#include <git2.h>
#include <curl/curl.h>
#include <archive.h>
#include <archive_entry.h>
#include <blake3.h>
#include <tbb/parallel_for.h>
#include <tbb/task_arena.h>

#include <aws/core/Aws.h>
#include <aws/core/auth/AWSCredentialsProvider.h>
#include <aws/core/client/ClientConfiguration.h>
#include <aws/core/http/HttpTypes.h>
#include <aws/core/utils/logging/LogLevel.h>
#include <aws/s3/S3Client.h>

#include <curl/curlver.h>
#include <openssl/evp.h>

#include "lua.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <stdexcept>
#include <string_view>
#include <system_error>

namespace {
std::string to_hex(const unsigned char *data, size_t length) {
  std::ostringstream oss;
  oss << std::hex << std::setfill('0');
  for (size_t i = 0; i < length; ++i) {
    oss << std::setw(2) << static_cast<int>(data[i]);
  }
  return oss.str();
}

class CurlGlobalGuard {
 public:
  CurlGlobalGuard() {
    const CURLcode rc = curl_global_init(CURL_GLOBAL_ALL);
    if (rc != CURLE_OK) {
      throw std::runtime_error("curl_global_init failed: " +
                               std::string(curl_easy_strerror(rc)));
    }
  }

  ~CurlGlobalGuard() { curl_global_cleanup(); }
};

class CurlEasyHandle {
 public:
  CurlEasyHandle() : handle_(curl_easy_init()) {
    if (!handle_) {
      throw std::runtime_error("curl_easy_init failed");
    }
  }

  ~CurlEasyHandle() {
    if (handle_) {
      curl_easy_cleanup(handle_);
    }
  }

  CURL *get() const { return handle_; }

 private:
 CURL *handle_{};
};

void apply_common_curl_options(CURL *curl) {
  curl_easy_setopt(curl, CURLOPT_NOPROXY, "*");
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
  curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "codex-cmake-test/1.0");
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L);
  curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
}

size_t write_stream_callback(void *ptr, size_t size, size_t nmemb, void *userdata) {
  auto *stream = static_cast<std::ofstream *>(userdata);
  const size_t total = size * nmemb;
  stream->write(static_cast<const char *>(ptr), static_cast<std::streamsize>(total));
  if (!*stream) {
    return 0;
  }
  return total;
}

class AwsApiGuard {
 public:
  AwsApiGuard() {
    options_.loggingOptions.logLevel = Aws::Utils::Logging::LogLevel::Error;
    Aws::InitAPI(options_);
  }

  AwsApiGuard(const AwsApiGuard &) = delete;
  AwsApiGuard &operator=(const AwsApiGuard &) = delete;

  ~AwsApiGuard() { Aws::ShutdownAPI(options_); }

 private:
  Aws::SDKOptions options_{};
};

size_t write_string_callback(void *ptr, size_t size, size_t nmemb, void *userdata) {
  auto *out = static_cast<std::string *>(userdata);
  const size_t total = size * nmemb;
  out->append(static_cast<const char *>(ptr), total);
  return total;
}

void download_file_to_path(const std::string &url, const std::filesystem::path &destination) {
  CurlEasyHandle handle;
  apply_common_curl_options(handle.get());

  std::ofstream output(destination, std::ios::binary);
  if (!output) {
    throw std::runtime_error("Failed to open " + destination.string() + " for writing");
  }

  char error_buffer[CURL_ERROR_SIZE] = {0};
  curl_easy_setopt(handle.get(), CURLOPT_ERRORBUFFER, error_buffer);
  curl_easy_setopt(handle.get(), CURLOPT_URL, url.c_str());
  curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, write_stream_callback);
  curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, &output);

  const CURLcode rc = curl_easy_perform(handle.get());
  output.flush();
  if (rc != CURLE_OK) {
    const std::string message = error_buffer[0] != '\0'
                                    ? std::string(error_buffer)
                                    : std::string(curl_easy_strerror(rc));
    throw std::runtime_error("curl failed to download " + url + ": " + message);
  }
  if (!output) {
    throw std::runtime_error("Failed to flush downloaded data to " + destination.string());
  }
}

std::string read_file_to_string(const std::filesystem::path &path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("Failed to open " + path.string() + " for reading");
  }
  std::ostringstream oss;
  oss << input.rdbuf();
  return oss.str();
}

std::string extract_sha256_from_text(const std::string &text) {
  std::istringstream iss(text);
  std::string token;
  while (iss >> token) {
    if (token.size() == 64 &&
        std::all_of(token.begin(), token.end(), [](unsigned char ch) { return std::isxdigit(ch) != 0; })) {
      std::string lowered = token;
      std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
      });
      return lowered;
    }
  }
  throw std::runtime_error("Failed to locate SHA256 digest in signature file");
}

std::array<unsigned char, 32> compute_sha256(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("Failed to open " + path.string() + " for hashing");
  }

  struct MdCtxDeleter {
    void operator()(EVP_MD_CTX *ctx) const {
      if (ctx) {
        EVP_MD_CTX_free(ctx);
      }
    }
  };

  std::unique_ptr<EVP_MD_CTX, MdCtxDeleter> ctx{EVP_MD_CTX_new()};
  if (!ctx) {
    throw std::runtime_error("OpenSSL failed to allocate SHA256 context");
  }
  if (EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr) != 1) {
    throw std::runtime_error("OpenSSL SHA256 init failed");
  }

  std::array<char, 1 << 15> buffer{};
  while (input.read(buffer.data(), buffer.size()) || input.gcount() > 0) {
    const auto read_bytes = static_cast<size_t>(input.gcount());
    if (read_bytes > 0) {
      if (EVP_DigestUpdate(ctx.get(), buffer.data(), read_bytes) != 1) {
        throw std::runtime_error("OpenSSL SHA256 update failed");
      }
    }
  }
  if (input.bad()) {
    throw std::runtime_error("Failed while reading " + path.string() + " for SHA256");
  }

  std::array<unsigned char, 32> digest{};
  unsigned int written = 0;
  if (EVP_DigestFinal_ex(ctx.get(), digest.data(), &written) != 1) {
    throw std::runtime_error("OpenSSL SHA256 finalization failed");
  }
  if (written != digest.size()) {
    throw std::runtime_error("OpenSSL SHA256 produced unexpected length");
  }
  return digest;
}

void archive_copy_data(struct archive *source, struct archive *dest) {
  const void *buff = nullptr;
  size_t size = 0;
  la_int64_t offset = 0;
  while (true) {
    const int r = archive_read_data_block(source, &buff, &size, &offset);
    if (r == ARCHIVE_EOF) {
      break;
    }
    if (r != ARCHIVE_OK) {
      throw std::runtime_error(std::string("libarchive read error: ") + archive_error_string(source));
    }
    const int write_result = archive_write_data_block(dest, buff, size, offset);
    if (write_result != ARCHIVE_OK) {
      throw std::runtime_error(std::string("libarchive write error: ") + archive_error_string(dest));
    }
  }
}

void extract_archive(const std::filesystem::path &archive_path,
                     const std::filesystem::path &destination) {
  archive *reader = archive_read_new();
  if (!reader) {
    throw std::runtime_error("libarchive read allocation failed");
  }
  archive_read_support_filter_all(reader);
  archive_read_support_format_all(reader);

  archive *writer = archive_write_disk_new();
  if (!writer) {
    archive_read_free(reader);
    throw std::runtime_error("libarchive write allocation failed");
  }
  archive_write_disk_set_options(writer, ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM |
                                         ARCHIVE_EXTRACT_ACL | ARCHIVE_EXTRACT_FFLAGS);
  archive_write_disk_set_standard_lookup(writer);

  if (archive_read_open_filename(reader, archive_path.string().c_str(), 10240) != ARCHIVE_OK) {
    const std::string message = std::string("libarchive failed to open ") + archive_path.string() +
                                ": " + archive_error_string(reader);
    archive_write_free(writer);
    archive_read_free(reader);
    throw std::runtime_error(message);
  }

  archive_entry *entry = nullptr;
  while (true) {
    const int r = archive_read_next_header(reader, &entry);
    if (r == ARCHIVE_EOF) {
      break;
    }
    if (r != ARCHIVE_OK) {
      const std::string message = std::string("libarchive failed to read header: ") +
                                  archive_error_string(reader);
      archive_read_close(reader);
      archive_write_close(writer);
      archive_read_free(reader);
      archive_write_free(writer);
      throw std::runtime_error(message);
    }

    const char *entry_path = archive_entry_pathname(entry);
    const std::filesystem::path full_path = destination / (entry_path ? entry_path : "");
    const auto parent = full_path.parent_path();
    if (!parent.empty()) {
      std::filesystem::create_directories(parent);
    }
    const std::string full_path_str = full_path.string();
    archive_entry_copy_pathname(entry, full_path_str.c_str());
    if (const char *hardlink = archive_entry_hardlink(entry)) {
      const auto hardlink_full = (destination / hardlink).string();
      archive_entry_copy_hardlink(entry, hardlink_full.c_str());
    }

    int write_header_result = archive_write_header(writer, entry);
    if (write_header_result != ARCHIVE_OK && write_header_result != ARCHIVE_WARN) {
      const std::string message = std::string("libarchive failed to write header: ") +
                                  archive_error_string(writer);
      archive_read_close(reader);
      archive_write_close(writer);
      archive_read_free(reader);
      archive_write_free(writer);
      throw std::runtime_error(message);
    }

    if (archive_entry_size(entry) > 0) {
      archive_copy_data(reader, writer);
    }
    if (archive_write_finish_entry(writer) != ARCHIVE_OK) {
      const std::string message = std::string("libarchive failed to finish entry: ") +
                                  archive_error_string(writer);
      archive_read_close(reader);
      archive_write_close(writer);
      archive_read_free(reader);
      archive_write_free(writer);
      throw std::runtime_error(message);
    }
  }

  archive_read_close(reader);
  archive_write_close(writer);
  archive_read_free(reader);
  archive_write_free(writer);
}

std::filesystem::path locate_arm_none_eabi_gcc(const std::filesystem::path &root) {
  std::error_code ec;
  std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::follow_directory_symlink, ec);
  if (ec) {
    throw std::runtime_error("Failed to start directory scan: " + ec.message());
  }
  const std::filesystem::recursive_directory_iterator end;
  for (; it != end; it.increment(ec)) {
    if (ec) {
      throw std::runtime_error("Failed while scanning extracted toolchain: " + ec.message());
    }
    const auto &path = it->path();
    std::error_code status_ec;
    const auto status = std::filesystem::status(path, status_ec);
    if (status_ec) {
      throw std::runtime_error("Failed to query status for " + path.string() + ": " + status_ec.message());
    }
    if (std::filesystem::is_regular_file(status) && path.filename() == "arm-none-eabi-gcc") {
      return path;
    }
  }
  throw std::runtime_error("arm-none-eabi-gcc not found in extracted archive");
}

std::array<uint8_t, BLAKE3_OUT_LEN> compute_blake3_file(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("Failed to open " + path.string() + " for BLAKE3 hashing");
  }

  blake3_hasher hasher;
  blake3_hasher_init(&hasher);

  std::array<char, 1 << 15> buffer{};
  while (input.read(buffer.data(), buffer.size()) || input.gcount() > 0) {
    const auto read_bytes = static_cast<size_t>(input.gcount());
    if (read_bytes > 0) {
      blake3_hasher_update(&hasher, buffer.data(), read_bytes);
    }
  }
  if (input.bad()) {
    throw std::runtime_error("Failed while reading " + path.string() + " for BLAKE3");
  }

  std::array<uint8_t, BLAKE3_OUT_LEN> digest{};
  blake3_hasher_finalize(&hasher, digest.data(), digest.size());
  return digest;
}

std::filesystem::path create_temp_directory() {
  const auto base = std::filesystem::temp_directory_path();
  if (!std::filesystem::exists(base)) {
    throw std::runtime_error("Temporary directory base does not exist");
  }

  std::random_device rd;
  std::mt19937_64 rng(rd());
  std::uniform_int_distribution<uint64_t> dist;

  std::ostringstream name;
  name << "codex-cmake-test-" << std::hex << std::setw(16) << std::setfill('0') << dist(rng);
  const auto candidate = base / name.str();

  std::error_code ec;
  std::filesystem::remove_all(candidate, ec);
  std::filesystem::create_directories(candidate);
  return candidate;
}

void probe_arm_toolchain() {
  static constexpr std::string_view kArchiveUrl =
      "https://developer.arm.com/-/media/Files/downloads/gnu/14.3.rel1/binrel/"
      "arm-gnu-toolchain-14.3.rel1-darwin-arm64-arm-none-eabi.tar.xz";
  static constexpr std::string_view kSignatureUrl =
      "https://developer.arm.com/-/media/Files/downloads/gnu/14.3.rel1/binrel/"
      "arm-gnu-toolchain-14.3.rel1-darwin-arm64-arm-none-eabi.tar.xz.sha256asc";

  std::cout << "[arm-toolchain] Preparing temporary workspace..." << std::endl;
  const auto temp_root = create_temp_directory();
  std::cout << "[arm-toolchain] Workspace directory: " << temp_root << std::endl;

  struct ScopedPath {
    explicit ScopedPath(std::filesystem::path p) : value(std::move(p)) {}
    ~ScopedPath() {
      if (!value.empty()) {
        std::error_code ec;
        std::filesystem::remove_all(value, ec);
      }
    }
    std::filesystem::path value;
  } cleanup(temp_root);

  const auto archive_path = temp_root / "arm-gnu-toolchain.tar.xz";
  const auto signature_path = temp_root / "arm-gnu-toolchain.tar.xz.sha256asc";
  const auto extract_dir = temp_root / "extract";
  std::filesystem::create_directories(extract_dir);
  std::cout << "[arm-toolchain] Extract destination: " << extract_dir << std::endl;

  CurlGlobalGuard curl_guard;

  std::cout << "[arm-toolchain] Downloading toolchain archive..." << std::endl;
  download_file_to_path(std::string(kArchiveUrl), archive_path);

  std::cout << "[arm-toolchain] Downloading SHA256 signature..." << std::endl;
  download_file_to_path(std::string(kSignatureUrl), signature_path);

  const auto signature_text = read_file_to_string(signature_path);
  const auto expected_digest = extract_sha256_from_text(signature_text);
  std::cout << "[arm-toolchain] Verifying SHA256 digest..." << std::endl;
  const auto computed_digest = compute_sha256(archive_path);
  const auto computed_hex = to_hex(computed_digest.data(), computed_digest.size());
  std::cout << "[arm-toolchain] Expected SHA256: " << expected_digest << std::endl;
  std::cout << "[arm-toolchain] Computed SHA256: " << computed_hex << std::endl;
  if (computed_hex != expected_digest) {
    throw std::runtime_error("SHA256 verification failed: expected " + expected_digest +
                             " but computed " + computed_hex);
  }

  std::cout << "[arm-toolchain] Extracting archive..." << std::endl;
  extract_archive(archive_path, extract_dir);

  std::uint64_t extracted_files = 0;
  std::error_code iter_ec;
  for (std::filesystem::recursive_directory_iterator it(extract_dir, std::filesystem::directory_options::follow_directory_symlink, iter_ec), end;
       it != end; it.increment(iter_ec)) {
    if (iter_ec) {
      throw std::runtime_error("Failed while counting extracted files: " + iter_ec.message());
    }
    std::error_code status_ec;
    const auto status = std::filesystem::status(it->path(), status_ec);
    if (status_ec) {
      throw std::runtime_error("Failed to query status during counting for " + it->path().string() +
                               ": " + status_ec.message());
    }
    if (std::filesystem::is_regular_file(status)) {
      ++extracted_files;
    }
  }
  if (iter_ec) {
    throw std::runtime_error("Failed to iterate extracted files: " + iter_ec.message());
  }
  std::cout << "[arm-toolchain] Extracted file count: " << extracted_files << std::endl;

  const auto gcc_path = locate_arm_none_eabi_gcc(extract_dir);
  const auto blake3_digest = compute_blake3_file(gcc_path);
  std::cout << "[arm-toolchain] arm-none-eabi-gcc BLAKE3: "
            << to_hex(blake3_digest.data(), blake3_digest.size()) << std::endl;

  std::error_code cleanup_ec;
  std::filesystem::remove_all(temp_root, cleanup_ec);
  if (cleanup_ec) {
    throw std::runtime_error("Failed to remove temporary workspace: " + cleanup_ec.message());
  }
  cleanup.value.clear();
  std::cout << "[arm-toolchain] Temporary workspace removed." << std::endl;
}

[[noreturn]] void throw_git_error(const char *context) {
  const git_error *err = git_error_last();
  if (err && err->message) {
    throw std::runtime_error(std::string(context) + ": " + err->message);
  }
  throw std::runtime_error(context);
}

void check_git() {
  std::cout << "[libgit2] Initializing runtime..." << std::endl;
  if (git_libgit2_init() < 0) {
    throw std::runtime_error("libgit2 failed to initialize");
  }
  const auto features = git_libgit2_features();
  if ((features & GIT_FEATURE_HTTPS) == 0) {
    git_libgit2_shutdown();
    throw std::runtime_error("libgit2 HTTPS transport unavailable");
  }
  if ((features & GIT_FEATURE_SSH) == 0) {
    git_libgit2_shutdown();
    throw std::runtime_error("libgit2 SSH transport unavailable");
  }

  const auto temp_dir = std::filesystem::temp_directory_path() /
                        std::filesystem::path("codex-git-probe-%%%%-%%%%");
  std::filesystem::create_directories(temp_dir);
  const auto temp_dir_str = temp_dir.string();

  git_repository *repo = nullptr;
  if (git_repository_init(&repo, temp_dir_str.c_str(), 1) != 0) {
    std::filesystem::remove_all(temp_dir);
    git_libgit2_shutdown();
    throw_git_error("libgit2 repository init failed");
  }

  static constexpr const char *kGitProbeUrl =
      "https://github.com/libgit2/libgit2.git";
  git_remote *remote = nullptr;
  if (git_remote_create_anonymous(&remote, repo, kGitProbeUrl) != 0) {
    git_repository_free(repo);
    std::filesystem::remove_all(temp_dir);
    git_libgit2_shutdown();
    throw_git_error("libgit2 remote creation failed");
  }

  std::cout << "[libgit2] Connecting to " << kGitProbeUrl
            << " for reference listing..." << std::endl;
  if (git_remote_connect(remote, GIT_DIRECTION_FETCH, nullptr, nullptr, nullptr) !=
      0) {
    git_remote_free(remote);
    git_repository_free(repo);
    std::filesystem::remove_all(temp_dir);
    git_libgit2_shutdown();
    throw_git_error("libgit2 remote connect failed");
  }

  const git_remote_head **refs = nullptr;
  size_t ref_count = 0;
  if (git_remote_ls(&refs, &ref_count, remote) != 0) {
    git_remote_disconnect(remote);
    git_remote_free(remote);
    git_repository_free(repo);
    std::filesystem::remove_all(temp_dir);
    git_libgit2_shutdown();
    throw_git_error("libgit2 remote reference enumeration failed");
  }

  if (ref_count == 0) {
    git_remote_disconnect(remote);
    git_remote_free(remote);
    git_repository_free(repo);
    std::filesystem::remove_all(temp_dir);
    git_libgit2_shutdown();
    throw std::runtime_error("libgit2 remote returned zero references");
  }

  std::cout << "[libgit2] Retrieved " << ref_count
            << " remote references; HEAD is " << refs[0]->name << std::endl;

  git_remote_disconnect(remote);
  git_remote_free(remote);
  git_repository_free(repo);
  std::error_code ignore_ec;
  std::filesystem::remove_all(temp_dir, ignore_ec);
  git_libgit2_shutdown();
  std::cout << "[libgit2] HTTPS and SSH transports are available." << std::endl;
}

void check_curl() {
  std::cout << "[libcurl] Global init and transport capability check..." << std::endl;
    CURLcode init_result = curl_global_init(CURL_GLOBAL_ALL);
    if (init_result != CURLE_OK) {
        throw std::runtime_error("libcurl global init failed");
    }
    CURL *curl = curl_easy_init();
    if (!curl) {
        curl_global_cleanup();
        throw std::runtime_error("libcurl easy handle creation failed");
    }
    curl_easy_setopt(curl, CURLOPT_NOPROXY, "*");
    const curl_version_info_data *info = curl_version_info(CURLVERSION_NOW);
    if (!info) {
        curl_easy_cleanup(curl);
        curl_global_cleanup();
        throw std::runtime_error("curl_version_info returned null");
    }
    if ((info->features & CURL_VERSION_SSL) == 0U) {
        curl_easy_cleanup(curl);
        curl_global_cleanup();
        throw std::runtime_error("libcurl built without TLS support");
    }
    if (!info->libssh_version || info->libssh_version[0] == '\0') {
        curl_easy_cleanup(curl);
        curl_global_cleanup();
        throw std::runtime_error("libcurl built without libssh2 support");
    }
    const std::string_view ssl_backend{info->ssl_version ? info->ssl_version : ""};
  if (ssl_backend.find("OpenSSL") == std::string_view::npos) {
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    throw std::runtime_error("libcurl not using OpenSSL");
  }

  static constexpr const char *kCurlProbeUrl = "https://github.com/";
  std::cout << "[libcurl] Performing HTTPS HEAD request to " << kCurlProbeUrl
            << "..." << std::endl;
  curl_easy_setopt(curl, CURLOPT_URL, kCurlProbeUrl);
  curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "codex-cmake-test/1.0");
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

  const CURLcode perform_result = curl_easy_perform(curl);
  if (perform_result != CURLE_OK) {
    const char *err = curl_easy_strerror(perform_result);
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    throw std::runtime_error(std::string("libcurl HTTPS request failed: ") +
                             (err ? err : "unknown error"));
  }

  long response_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
  if (response_code < 200 || response_code >= 400) {
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    throw std::runtime_error("libcurl HTTPS probe returned non-success status: " +
                             std::to_string(response_code));
  }

  curl_easy_cleanup(curl);
  curl_global_cleanup();
  std::cout << "[libcurl] TLS backend: "
            << (info->ssl_version ? info->ssl_version : "unknown")
            << "; libssh2: "
            << (info->libssh_version ? info->libssh_version : "none")
            << "; response code " << response_code << std::endl;
}

void check_s3_presign() {
  std::cout << "[aws-sdk] Generating presigned S3 URL..." << std::endl;
  AwsApiGuard guard;

  const Aws::Auth::AWSCredentials credentials("AKIDCODExAMPLE", "verySecretKeyExample");
  auto provider = std::make_shared<Aws::Auth::SimpleAWSCredentialsProvider>(credentials);

  Aws::Client::ClientConfiguration config;
  config.region = "us-east-1";
  config.scheme = Aws::Http::Scheme::HTTPS;
  config.verifySSL = true;
  config.connectTimeoutMs = 2000;
  config.requestTimeoutMs = 5000;

  Aws::S3::S3Client client(provider, config,
                           Aws::Client::AWSAuthV4Signer::PayloadSigningPolicy::Never,
                           false);

  const Aws::String bucket = "codex-cmake-test-bucket";
  const Aws::String key = "smoke-object.txt";
  const Aws::String presigned = client.GeneratePresignedUrl(
      bucket, key, Aws::Http::HttpMethod::HTTP_PUT, 900);

  const std::string presigned_utf8 = presigned.c_str();
  if (presigned_utf8.empty()) {
    throw std::runtime_error("[aws-sdk] Presigned URL generation returned empty string");
  }
  if (presigned_utf8.find(bucket.c_str()) == std::string::npos ||
      presigned_utf8.find(key.c_str()) == std::string::npos) {
    throw std::runtime_error("[aws-sdk] Presigned URL missing bucket or key tokens");
  }
  if (presigned_utf8.rfind("https://", 0) != 0) {
    throw std::runtime_error("[aws-sdk] Presigned URL does not use HTTPS: " + presigned_utf8);
  }

  std::cout << "[aws-sdk] Presigned URL generated successfully." << std::endl;
}

void check_lua() {
    std::cout << "[Lua] Spinning up interpreter..." << std::endl;
    lua_State *L = luaL_newstate();
    if (!L) {
        throw std::runtime_error("luaL_newstate failed");
    }
    luaL_openlibs(L);
    if (luaL_dostring(L, "return 2 + 2") != LUA_OK) {
        lua_close(L);
        throw std::runtime_error("lua execution failed");
    }
    if (!lua_isinteger(L, -1) || lua_tointeger(L, -1) != 4) {
        lua_pop(L, 1);
        lua_close(L);
        throw std::runtime_error("lua returned unexpected result");
    }
    lua_pop(L, 1);
    lua_close(L);
    std::cout << "[Lua] Interpreter opened, executed script, and shutdown cleanly." << std::endl;
}

void check_tbb() {
    std::cout << "[oneTBB] Running parallel_for smoke test..." << std::endl;
    std::array<int, 8> values{};
    tbb::task_arena arena;
    arena.execute([&] {
        tbb::parallel_for(std::size_t{0}, values.size(), [&](std::size_t idx) {
            values[idx] = static_cast<int>(idx * idx);
        });
    });
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (values[i] != static_cast<int>(i * i)) {
            throw std::runtime_error("oneTBB parallel_for produced unexpected data");
        }
    }
    std::cout << "[oneTBB] parallel_for produced expected results." << std::endl;
}

void check_libarchive() {
  std::cout << "[libarchive] Creating archive in-memory..." << std::endl;
  static constexpr std::string_view payload = "archive-smoke-test";

    archive *writer = archive_write_new();
    if (!writer) {
        throw std::runtime_error("libarchive writer allocation failed");
    }
    archive_write_set_format_pax_restricted(writer);
    archive_write_add_filter_none(writer);

    std::array<char, 512> buffer{};
    size_t written = 0;
    if (archive_write_open_memory(writer, buffer.data(), buffer.size(), &written) != ARCHIVE_OK) {
        archive_write_free(writer);
        throw std::runtime_error("libarchive open memory failed");
    }

    archive_entry *entry = archive_entry_new();
    if (!entry) {
        archive_write_close(writer);
        archive_write_free(writer);
        throw std::runtime_error("libarchive entry allocation failed");
    }
    archive_entry_set_pathname(entry, "payload.txt");
    archive_entry_set_filetype(entry, AE_IFREG);
    archive_entry_set_perm(entry, 0644);
    archive_entry_set_size(entry, payload.size());

    if (archive_write_header(writer, entry) != ARCHIVE_OK) {
        archive_entry_free(entry);
        archive_write_close(writer);
        archive_write_free(writer);
        throw std::runtime_error("libarchive write header failed");
    }

    if (archive_write_data(writer, payload.data(), payload.size()) != static_cast<ssize_t>(payload.size())) {
        archive_entry_free(entry);
        archive_write_close(writer);
        archive_write_free(writer);
        throw std::runtime_error("libarchive write data failed");
    }

    archive_entry_free(entry);
    archive_write_close(writer);
    archive_write_free(writer);
  std::cout << "[libarchive] Wrote and freed PAX archive successfully." << std::endl;
}

void check_blake3() {
  std::cout << "[BLAKE3] Hashing smoke payload..." << std::endl;
    static constexpr std::string_view message = "blake3-smoke";
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, message.data(), message.size());

    std::array<uint8_t, BLAKE3_OUT_LEN> digest{};
    blake3_hasher_finalize(&hasher, digest.data(), digest.size());

    if (std::all_of(digest.begin(), digest.end(), [](uint8_t byte) { return byte == 0; })) {
        throw std::runtime_error("BLAKE3 produced all-zero digest");
    }

    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, message.data(), message.size());
    std::array<uint8_t, BLAKE3_OUT_LEN> digest_second{};
    blake3_hasher_finalize(&hasher, digest_second.data(), digest_second.size());

    if (digest != digest_second) {
        throw std::runtime_error("BLAKE3 digests are not deterministic");
    }
  std::cout << "[BLAKE3] Digest computed and verified." << std::endl;
}

void check_md5() {
  std::cout << "[md5] Hashing test vector via OpenSSL..." << std::endl;
  static constexpr std::string_view message =
      "The quick brown fox jumps over the lazy dog";
  static constexpr std::size_t kMd5DigestLength = 16;
  std::array<unsigned char, kMd5DigestLength> digest{};

  struct MdCtxDeleter {
    void operator()(EVP_MD_CTX *ctx) const {
      if (ctx) {
        EVP_MD_CTX_free(ctx);
      }
    }
  };

  std::unique_ptr<EVP_MD_CTX, MdCtxDeleter> ctx{EVP_MD_CTX_new()};
  if (!ctx) {
    throw std::runtime_error("OpenSSL failed to allocate MD context");
  }

  if (EVP_DigestInit_ex(ctx.get(), EVP_md5(), nullptr) != 1) {
    throw std::runtime_error("OpenSSL EVP_MD init failed");
  }
  if (EVP_DigestUpdate(ctx.get(), message.data(), message.size()) != 1) {
    throw std::runtime_error("OpenSSL EVP_MD update failed");
  }

  unsigned int written = 0;
  if (EVP_DigestFinal_ex(ctx.get(), digest.data(), &written) != 1) {
    throw std::runtime_error("OpenSSL EVP_MD finalization failed");
  }
  if (written != digest.size()) {
    throw std::runtime_error("OpenSSL MD5 produced unexpected length");
  }

  static constexpr std::array<unsigned char, kMd5DigestLength> expected_bytes{
      0x9e, 0x10, 0x7d, 0x9d, 0x37, 0x2b, 0xb6, 0x82,
      0x6b, 0xd8, 0x1d, 0x35, 0x42, 0xa4, 0x19, 0xd6};
  if (!std::equal(digest.begin(), digest.end(), expected_bytes.begin())) {
    throw std::runtime_error("OpenSSL MD5 produced unexpected digest: " +
                             to_hex(digest.data(), digest.size()));
  }

  std::cout << "[md5] Digest " << to_hex(digest.data(), digest.size())
            << " validated." << std::endl;
}
} // namespace

int main() {
  try {
    std::cout << "=== codex-cmake-test dependency probe ===" << std::endl;
        check_git();
        check_curl();
        check_s3_presign();
        check_lua();
        check_tbb();
        check_libarchive();
        check_blake3();
        check_md5();
        probe_arm_toolchain();
    } catch (const std::exception &ex) {
        std::cerr << "Initialization failed: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "All dependencies initialized successfully." << std::endl;
    return EXIT_SUCCESS;
}

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winsock2.h>
#endif

#include "archive.h"
#include "archive_entry.h"

#include "aws/core/Aws.h"
#include "aws/core/Version.h"
#include "aws/core/auth/AWSCredentialsProviderChain.h"
#include "aws/core/auth/SSOCredentialsProvider.h"
#include "aws/core/client/ClientConfiguration.h"
#include "aws/core/http/HttpTypes.h"
#include "aws/core/platform/Environment.h"
#include "aws/core/utils/logging/LogLevel.h"
#include "aws/core/utils/logging/NullLogSystem.h"
#include "aws/crt/Api.h"
#include "aws/s3/S3Client.h"
#include "aws/s3/model/GetObjectRequest.h"
#include "aws/s3/model/HeadObjectRequest.h"

#include "blake3.h"

#include "CLI11.hpp"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

#include "mbedtls/sha256.h"
#include "mbedtls/version.h"

#include "bzlib.h"
#include "curl/curl.h"
#include "lzma.h"
#include "zlib.h"
#include "zstd.h"

#include "git2.h"
#include "oneapi/tbb/version.h"

#include "tbb/flow_graph.h"
#include "tbb/task_arena.h"

#include "libssh2.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace {

void set_env_var(char const *name, char const *value) {
#ifdef _WIN32
  _putenv_s(name, value);
#else
  ::setenv(name, value, 1);
#endif
}

std::string to_hex(uint8_t const *data, size_t length) {
  std::ostringstream oss;
  oss << std::hex << std::setfill('0');
  for (size_t i = 0; i < length; ++i) {
    oss << std::setw(2) << static_cast<int>(data[i]);
  }
  return oss.str();
}

class AwsApiGuard {
 public:
  AwsApiGuard() {
    set_env_var("AWS_SDK_LOAD_CONFIG", "1");
    options_.loggingOptions.logLevel = Aws::Utils::Logging::LogLevel::Off;
    options_.loggingOptions.logger_create_fn = []() {
      return Aws::MakeShared<Aws::Utils::Logging::NullLogSystem>(
          "envy-cmake-test-logging");
    };
    Aws::InitAPI(options_);
  }

  AwsApiGuard(AwsApiGuard const &) = delete;
  AwsApiGuard &operator=(AwsApiGuard const &) = delete;

  ~AwsApiGuard() {
    Aws::ShutdownAPI(options_);
  }

 private:
  Aws::SDKOptions options_{};
};

std::filesystem::path create_temp_directory() {
  auto const base = std::filesystem::temp_directory_path();
  if (!std::filesystem::exists(base)) {
    throw std::runtime_error("Temporary directory base does not exist");
  }

  std::random_device rd;
  std::mt19937_64 rng(rd());
  std::uniform_int_distribution<uint64_t> dist;

  std::ostringstream name;
  name << "envy-cmake-test-" << std::hex << std::setw(16) << std::setfill('0')
       << dist(rng);
  auto const candidate = base / name.str();

  std::error_code ec;
  std::filesystem::remove_all(candidate, ec);
  std::filesystem::create_directories(candidate);
  return candidate;
}

class TempResourceManager {
 public:
  TempResourceManager() = default;
  TempResourceManager(TempResourceManager const &) = delete;
  TempResourceManager &operator=(TempResourceManager const &) = delete;
  ~TempResourceManager() {
    cleanup();
  }

  std::filesystem::path create_directory() {
    auto dir = create_temp_directory();
    tracked_directories_.push_back(dir);
    return dir;
  }

  void cleanup() noexcept {
    for (auto it = tracked_directories_.rbegin(); it != tracked_directories_.rend();
         ++it) {
      std::error_code ec;
      std::filesystem::remove_all(*it, ec);
      if (ec) {
        std::cerr << "[cleanup] Failed to remove " << *it << ": " << ec.message()
                  << std::endl;
      }
    }
    tracked_directories_.clear();
  }

 private:
  std::vector<std::filesystem::path> tracked_directories_{};
};

TempResourceManager *g_temp_manager = nullptr;

class TempManagerScope {
 public:
  explicit TempManagerScope(TempResourceManager &manager) : manager_(manager) {
    g_temp_manager = &manager_;
  }

  TempManagerScope(TempManagerScope const &) = delete;
  TempManagerScope &operator=(TempManagerScope const &) = delete;
  ~TempManagerScope() {
    g_temp_manager = nullptr;
  }

 private:
  TempResourceManager &manager_;
};

struct S3UriParts {
  std::string bucket;
  std::string key;
};

S3UriParts parse_s3_uri(std::string_view uri) {
  static constexpr std::string_view kScheme = "s3://";
  if (!uri.starts_with(kScheme)) {
    throw std::invalid_argument("S3 URI must start with s3://");
  }
  std::string_view remainder = uri.substr(kScheme.size());
  auto const slash = remainder.find('/');
  if (slash == std::string_view::npos || slash == 0 || slash + 1 >= remainder.size()) {
    throw std::invalid_argument(
        "S3 URI must include bucket and key, e.g. s3://bucket/key");
  }
  S3UriParts parts;
  parts.bucket = std::string(remainder.substr(0, slash));
  parts.key = std::string(remainder.substr(slash + 1));
  return parts;
}

std::shared_ptr<Aws::Auth::AWSCredentialsProvider> select_credentials_provider(
    Aws::S3::S3ClientConfiguration const &s3_config) {
  static constexpr char const *kAllocationTag = "envy-cmake-test-sso";
  Aws::String const profile_from_env = Aws::Environment::GetEnv("AWS_PROFILE");
  Aws::String const profile =
      profile_from_env.empty() ? Aws::Auth::GetConfigProfileName() : profile_from_env;
  try {
    auto client_config = Aws::MakeShared<Aws::Client::ClientConfiguration>(kAllocationTag);
    *client_config = static_cast<Aws::Client::ClientConfiguration const &>(s3_config);
    auto sso_provider = Aws::MakeShared<Aws::Auth::SSOCredentialsProvider>(
        kAllocationTag,
        profile,
        std::static_pointer_cast<Aws::Client::ClientConfiguration const>(client_config));
    std::cout << "[aws-sdk] Using AWS SSO credentials provider for profile '" << profile
              << "'." << std::endl;
    return sso_provider;
  } catch (std::exception const &ex) {
    std::cout << "[aws-sdk] AWS SSO provider initialization failed: " << ex.what()
              << "; falling back to default provider chain." << std::endl;
  }
  return Aws::MakeShared<Aws::Auth::DefaultAWSCredentialsProviderChain>(kAllocationTag);
}

void archive_copy_data(struct archive *source, struct archive *dest) {
  void const *buff = nullptr;
  size_t size = 0;
  la_int64_t offset = 0;
  while (true) {
    int const r = archive_read_data_block(source, &buff, &size, &offset);
    if (r == ARCHIVE_EOF) {
      break;
    }
    if (r != ARCHIVE_OK) {
      throw std::runtime_error(std::string("libarchive read error: ") +
                               archive_error_string(source));
    }
    int const write_result = archive_write_data_block(dest, buff, size, offset);
    if (write_result != ARCHIVE_OK) {
      throw std::runtime_error(std::string("libarchive write error: ") +
                               archive_error_string(dest));
    }
  }
}

std::uint64_t extract_archive(std::filesystem::path const &archive_path,
                              std::filesystem::path const &destination) {
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
  archive_write_disk_set_options(writer,
                                 ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM |
                                     ARCHIVE_EXTRACT_ACL | ARCHIVE_EXTRACT_FFLAGS);
  archive_write_disk_set_standard_lookup(writer);

  if (archive_read_open_filename(reader, archive_path.string().c_str(), 10240) !=
      ARCHIVE_OK) {
    std::string const message = std::string("libarchive failed to open ") +
                                archive_path.string() + ": " +
                                archive_error_string(reader);
    archive_write_free(writer);
    archive_read_free(reader);
    throw std::runtime_error(message);
  }

  archive_entry *entry = nullptr;
  std::uint64_t regular_files = 0;
  while (true) {
    int const r = archive_read_next_header(reader, &entry);
    if (r == ARCHIVE_EOF) {
      break;
    }
    if (r != ARCHIVE_OK) {
      std::string const message =
          std::string("libarchive failed to read header: ") + archive_error_string(reader);
      archive_read_close(reader);
      archive_write_close(writer);
      archive_read_free(reader);
      archive_write_free(writer);
      throw std::runtime_error(message);
    }

    char const *entry_path = archive_entry_pathname(entry);
    std::filesystem::path const full_path = destination / (entry_path ? entry_path : "");
    auto const parent = full_path.parent_path();
    if (!parent.empty()) {
      std::filesystem::create_directories(parent);
    }
    std::string const full_path_str = full_path.string();
    archive_entry_copy_pathname(entry, full_path_str.c_str());
    if (char const *hardlink = archive_entry_hardlink(entry)) {
      auto const hardlink_full = (destination / hardlink).string();
      archive_entry_copy_hardlink(entry, hardlink_full.c_str());
    }

    int write_header_result = archive_write_header(writer, entry);
    if (write_header_result != ARCHIVE_OK && write_header_result != ARCHIVE_WARN) {
      std::string const message = std::string("libarchive failed to write header: ") +
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
      std::string const message = std::string("libarchive failed to finish entry: ") +
                                  archive_error_string(writer);
      archive_read_close(reader);
      archive_write_close(writer);
      archive_read_free(reader);
      archive_write_free(writer);
      throw std::runtime_error(message);
    }

    if (archive_entry_filetype(entry) == AE_IFREG) {
      ++regular_files;
    }
  }

  archive_read_close(reader);
  archive_write_close(writer);
  archive_read_free(reader);
  archive_write_free(writer);

  return regular_files;
}

std::array<uint8_t, BLAKE3_OUT_LEN> compute_blake3_file(
    std::filesystem::path const &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("Failed to open " + path.string() + " for BLAKE3 hashing");
  }

  blake3_hasher hasher;
  blake3_hasher_init(&hasher);

  std::array<char, 1 << 15> buffer{};
  while (input.read(buffer.data(), buffer.size()) || input.gcount() > 0) {
    auto const read_bytes = static_cast<size_t>(input.gcount());
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

constexpr size_t kSha256DigestLength = 32;

std::array<uint8_t, kSha256DigestLength> compute_sha256_file(
    std::filesystem::path const &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("Failed to open " + path.string() + " for SHA256 hashing");
  }

  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  struct Sha256ContextGuard {
    mbedtls_sha256_context *context;
    ~Sha256ContextGuard() {
      if (context) {
        mbedtls_sha256_free(context);
      }
    }
  } guard{ &ctx };
  if (mbedtls_sha256_starts(&ctx, /*is224=*/0) != 0) {
    throw std::runtime_error("mbedtls_sha256_starts failed");
  }

  std::array<char, 1 << 15> buffer{};
  while (input.read(buffer.data(), buffer.size()) || input.gcount() > 0) {
    auto const read_bytes = static_cast<size_t>(input.gcount());
    if (read_bytes > 0) {
      unsigned char const *data = reinterpret_cast<unsigned char const *>(buffer.data());
      if (mbedtls_sha256_update(&ctx, data, read_bytes) != 0) {
        throw std::runtime_error("mbedtls_sha256_update failed for " + path.string());
      }
    }
  }
  if (input.bad()) {
    throw std::runtime_error("Failed while reading " + path.string() + " for SHA256");
  }

  std::array<uint8_t, kSha256DigestLength> digest{};
  if (mbedtls_sha256_finish(&ctx, digest.data()) != 0) {
    throw std::runtime_error("mbedtls_sha256_finish failed");
  }
  return digest;
}

std::vector<std::filesystem::path> collect_first_regular_files(
    std::filesystem::path const &root,
    std::size_t max_count) {
  std::vector<std::filesystem::path> files;
  files.reserve(max_count);

  std::error_code ec;
  for (std::filesystem::recursive_directory_iterator
           it(root, std::filesystem::directory_options::follow_directory_symlink, ec),
       end;
       it != end && files.size() < max_count;
       it.increment(ec)) {
    if (ec) {
      throw std::runtime_error("Failed to iterate " + root.string() + ": " + ec.message());
    }
    std::error_code status_ec;
    auto const status = std::filesystem::status(it->path(), status_ec);
    if (status_ec) {
      throw std::runtime_error("Failed to query status for " + it->path().string() + ": " +
                               status_ec.message());
    }
    if (std::filesystem::is_regular_file(status)) {
      files.push_back(it->path());
    }
  }
  if (ec) {
    throw std::runtime_error("Failed to finalize iteration for " + root.string() + ": " +
                             ec.message());
  }
  return files;
}

std::string relative_display(std::filesystem::path const &path,
                             std::filesystem::path const &base) {
  std::error_code ec;
  auto const rel = std::filesystem::relative(path, base, ec);
  if (!ec) {
    auto const normalized = rel.lexically_normal();
    if (!normalized.empty()) {
      return normalized.generic_string();
    }
  }
  return path.filename().generic_string();
}

std::filesystem::path download_s3_object(TempResourceManager &manager,
                                         std::string const &bucket,
                                         std::string const &key,
                                         std::string const &region) {
  if (bucket.empty()) {
    throw std::runtime_error("S3 bucket name must not be empty");
  }
  if (key.empty()) {
    throw std::runtime_error("S3 object key must not be empty");
  }

  auto const temp_dir = manager.create_directory();
  auto const file_name = std::filesystem::path(key).filename();
  if (file_name.empty()) {
    throw std::runtime_error("S3 object key does not contain a filename");
  }
  auto const destination = temp_dir / file_name;

  Aws::S3::S3ClientConfiguration config;
  if (!region.empty()) {
    config.region = region.c_str();
  }
  config.scheme = Aws::Http::Scheme::HTTPS;
  config.verifySSL = true;
  config.connectTimeoutMs = 3000;
  config.requestTimeoutMs = 30000;

  auto const credentials_provider = select_credentials_provider(config);
  Aws::S3::S3Client client(credentials_provider, nullptr, config);

  long long expected_size = -1;
  {
    Aws::S3::Model::HeadObjectRequest head_request;
    head_request.SetBucket(bucket.c_str());
    head_request.SetKey(key.c_str());
    auto const head_outcome = client.HeadObject(head_request);
    if (head_outcome.IsSuccess()) {
      expected_size = head_outcome.GetResult().GetContentLength();
    }
  }

  Aws::S3::Model::GetObjectRequest request;
  request.SetBucket(bucket.c_str());
  request.SetKey(key.c_str());

  struct ProgressState {
    std::mutex mutex;
    long long downloaded = 0;
    std::chrono::steady_clock::time_point last_update = std::chrono::steady_clock::now();
  };
  auto const progress_state = std::make_shared<ProgressState>();

  request.SetDataReceivedEventHandler(
      [progress_state, expected_size](Aws::Http::HttpRequest const *,
                                      Aws::Http::HttpResponse *,
                                      long long bytes_transferred) {
        if (bytes_transferred <= 0) {
          return;
        }
        std::lock_guard<std::mutex> lock(progress_state->mutex);
        progress_state->downloaded += bytes_transferred;
        auto const now = std::chrono::steady_clock::now();
        if (now - progress_state->last_update < std::chrono::milliseconds(200)) {
          return;
        }
        progress_state->last_update = now;
        std::cout << '\r';
        if (expected_size > 0) {
          double const percent = static_cast<double>(progress_state->downloaded) * 100.0 /
                                 static_cast<double>(expected_size);
          double const clamped = percent > 100.0 ? 100.0 : percent;
          std::cout << "[aws-sdk] Download progress: " << std::fixed
                    << std::setprecision(1) << clamped << "%";
        } else {
          double const mebibytes =
              static_cast<double>(progress_state->downloaded) / (1024.0 * 1024.0);
          std::cout << "[aws-sdk] Downloaded " << std::fixed << std::setprecision(2)
                    << mebibytes << " MiB";
        }
        std::cout << std::flush;
      });

  auto const download_start = std::chrono::steady_clock::now();
  auto outcome = client.GetObject(request);
  auto const download_end = std::chrono::steady_clock::now();

  {
    std::lock_guard<std::mutex> lock(progress_state->mutex);
    if (progress_state->downloaded > 0) {
      std::cout << '\r';
      if (expected_size > 0) {
        std::cout << "[aws-sdk] Download progress: 100.0%";
      } else {
        double const mebibytes =
            static_cast<double>(progress_state->downloaded) / (1024.0 * 1024.0);
        std::cout << "[aws-sdk] Downloaded " << std::fixed << std::setprecision(2)
                  << mebibytes << " MiB";
      }
      std::cout << std::string(10, ' ') << '\n';
    }
  }

  if (!outcome.IsSuccess()) {
    auto const &error = outcome.GetError();
    throw std::runtime_error("S3 GetObject failed: " + error.GetMessage());
  }

  auto result = outcome.GetResultWithOwnership();
  std::ofstream output(destination, std::ios::binary);
  if (!output) {
    throw std::runtime_error("Failed to open " + destination.string() + " for writing");
  }
  output << result.GetBody().rdbuf();
  output.flush();
  if (!output) {
    throw std::runtime_error("Failed while writing S3 object to " + destination.string());
  }

  auto const sha256 = compute_sha256_file(destination);
  auto const elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(download_end - download_start);
  std::cout << "[aws-sdk] Downloaded s3://" << bucket << '/' << key << " to "
            << destination << " in " << std::fixed << std::setprecision(3)
            << static_cast<double>(elapsed.count()) / 1000.0 << "s" << std::endl;
  std::cout << "[aws-sdk] SHA256(" << destination.filename()
            << ") = " << to_hex(sha256.data(), sha256.size()) << std::endl;
  return destination;
}

int lua_download_s3_object(lua_State *L) {
  int const argc = lua_gettop(L);
  if (argc < 2 || argc > 3) {
    return luaL_error(L, "download_s3_object expects bucket, key, [region]");
  }
  if (!g_temp_manager) {
    return luaL_error(L, "temporary resource manager is not initialized");
  }

  std::string const bucket = luaL_checkstring(L, 1);
  std::string const key = luaL_checkstring(L, 2);
  std::string const region = argc >= 3 ? luaL_checkstring(L, 3) : "";

  try {
    auto const archive_path = download_s3_object(*g_temp_manager, bucket, key, region);
    lua_pushstring(L, archive_path.string().c_str());
    return 1;
  } catch (std::exception const &ex) {
    return luaL_error(L, "download_s3_object failed: %s", ex.what());
  }
}

int lua_extract_to_temp(lua_State *L) {
  std::string const archive = luaL_checkstring(L, 1);
  if (!g_temp_manager) {
    return luaL_error(L, "temporary resource manager is not initialized");
  }

  try {
    auto const destination = g_temp_manager->create_directory();
    auto const count = extract_archive(archive, destination);
    std::cout << "[lua] Extracted " << count << " files" << std::endl;

    auto const sample_files = collect_first_regular_files(destination, 5);
    if (sample_files.empty()) {
      std::cout << "[lua] No regular files discovered in archive." << std::endl;
    } else {
      std::size_t index = 1;
      for (auto const &file_path : sample_files) {
        auto const digest = compute_blake3_file(file_path);
        std::cout << "[lua] BLAKE3 sample " << index++ << ": "
                  << relative_display(file_path, destination) << " => "
                  << to_hex(digest.data(), digest.size()) << std::endl;
      }
    }

    lua_pushstring(L, destination.string().c_str());
    lua_pushinteger(L, static_cast<lua_Integer>(count));
    return 2;
  } catch (std::exception const &ex) {
    return luaL_error(L, "extract_to_temp failed: %s", ex.what());
  }
}

static constexpr char kLuaScript[] = R"(local bucket = assert(bucket, "bucket must be set")
local key = assert(key, "key must be set")
local region = region or ""

local archive_path = download_s3_object(bucket, key, region)
extract_to_temp(archive_path)
)";

std::string format_git_error(int error_code) {
  git_error const *error = git_error_last();
  std::ostringstream oss;
  oss << "libgit2 error (" << error_code << ')';
  if (error && error->message) {
    oss << ": " << error->message;
  }
  return oss.str();
}

void run_git_tls_probe(std::string const &url,
                       std::filesystem::path const &workspace_root,
                       std::mutex &console_mutex) {
  git_libgit2_init();
  git_repository *repo = nullptr;
  git_remote *remote = nullptr;
  auto const probe_dir = workspace_root / "out" / "cache" / "git_tls_probe";
  std::error_code fs_ec;
  std::filesystem::remove_all(probe_dir, fs_ec);
  std::filesystem::create_directories(probe_dir, fs_ec);

  try {
    git_repository_init_options opts = GIT_REPOSITORY_INIT_OPTIONS_INIT;
    opts.flags = GIT_REPOSITORY_INIT_BARE;
    int const init_result =
        git_repository_init_ext(&repo, probe_dir.string().c_str(), &opts);
    if (init_result != 0) {
      throw std::runtime_error(format_git_error(init_result));
    }

    if (git_remote_lookup(&remote, repo, "origin") == 0) {
      git_remote_delete(repo, "origin");
      git_remote_free(remote);
      remote = nullptr;
    }

    int const create_result = git_remote_create(&remote, repo, "origin", url.c_str());
    if (create_result != 0) {
      throw std::runtime_error(format_git_error(create_result));
    }

    int const connect_result =
        git_remote_connect(remote, GIT_DIRECTION_FETCH, nullptr, nullptr, nullptr);
    if (connect_result != 0) {
      throw std::runtime_error(format_git_error(connect_result));
    }

    git_remote_head const **heads = nullptr;
    size_t head_count = 0;
    int const ls_result = git_remote_ls(&heads, &head_count, remote);
    if (ls_result != 0) {
      throw std::runtime_error(format_git_error(ls_result));
    }

    {
      std::lock_guard<std::mutex> lock(console_mutex);
      std::cout << "[libgit2] Connected to " << url << " and enumerated " << head_count
                << " refs" << std::endl;
      if (head_count > 0 && heads[0] && heads[0]->name) {
        std::cout << "  First ref: " << heads[0]->name << std::endl;
      }
    }

    git_remote_disconnect(remote);
  } catch (...) {
    git_remote_free(remote);
    git_repository_free(repo);
    git_libgit2_shutdown();
    std::filesystem::remove_all(probe_dir, fs_ec);
    throw;
  }

  git_remote_free(remote);
  git_repository_free(repo);
  git_libgit2_shutdown();
  std::filesystem::remove_all(probe_dir, fs_ec);
}

size_t curl_write_callback(char *ptr, size_t size, size_t nmemb, void *userdata) {
  auto *buffer = static_cast<std::vector<char> *>(userdata);
  size_t const total = size * nmemb;
  buffer->insert(buffer->end(), ptr, ptr + total);
  return total;
}

void ensure_curl_initialized() {
  static std::once_flag once;
  std::call_once(once, [] {
    CURLcode const init_result = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (init_result != CURLE_OK) {
      throw std::runtime_error(std::string("curl_global_init failed: ") +
                               curl_easy_strerror(init_result));
    }
  });
}

void run_curl_tls_probe(std::string const &url, std::mutex &console_mutex) {
  ensure_curl_initialized();

  std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> handle(curl_easy_init(),
                                                             &curl_easy_cleanup);
  if (!handle) {
    throw std::runtime_error("curl_easy_init failed");
  }

  std::vector<char> response;
  curl_easy_setopt(handle.get(), CURLOPT_URL, url.c_str());
  curl_easy_setopt(handle.get(), CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(handle.get(), CURLOPT_USERAGENT, "envy-tls-probe/1.0");
  curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, curl_write_callback);
  curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(handle.get(), CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_3);

  CURLcode const perform_result = curl_easy_perform(handle.get());
  long http_code = 0;
  curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &http_code);

  if (perform_result != CURLE_OK) {
    throw std::runtime_error(std::string("curl_easy_perform failed: ") +
                             curl_easy_strerror(perform_result));
  }

  {
    std::lock_guard<std::mutex> lock(console_mutex);
    std::cout << "[libcurl] Downloaded " << response.size() << " bytes from " << url
              << " (HTTP " << http_code << ')' << std::endl;
  }
}

void run_lua_workflow(std::string const &bucket,
                      std::string const &key,
                      std::string const &region,
                      std::mutex &console_mutex) {
  AwsApiGuard aws_guard;
  TempResourceManager temp_manager;
  TempManagerScope manager_scope(temp_manager);

  std::unique_ptr<lua_State, decltype(&lua_close)> state(luaL_newstate(), &lua_close);
  if (!state) {
    throw std::runtime_error("luaL_newstate returned null");
  }
  luaL_openlibs(state.get());

  lua_pushcfunction(state.get(), lua_download_s3_object);
  lua_setglobal(state.get(), "download_s3_object");
  lua_pushcfunction(state.get(), lua_extract_to_temp);
  lua_setglobal(state.get(), "extract_to_temp");

  lua_pushlstring(state.get(), bucket.c_str(), static_cast<lua_Integer>(bucket.size()));
  lua_setglobal(state.get(), "bucket");
  lua_pushlstring(state.get(), key.c_str(), static_cast<lua_Integer>(key.size()));
  lua_setglobal(state.get(), "key");
  lua_pushlstring(state.get(), region.c_str(), static_cast<lua_Integer>(region.size()));
  lua_setglobal(state.get(), "region");

  if (luaL_loadstring(state.get(), kLuaScript) != LUA_OK) {
    char const *message = lua_tostring(state.get(), -1);
    throw std::runtime_error(std::string("Failed to load Lua script: ") +
                             (message ? message : "unknown error"));
  }
  if (lua_pcall(state.get(), 0, LUA_MULTRET, 0) != LUA_OK) {
    char const *message = lua_tostring(state.get(), -1);
    throw std::runtime_error(std::string("Lua script execution failed: ") +
                             (message ? message : "unknown error"));
  }

  {
    std::lock_guard<std::mutex> lock(console_mutex);
    std::cout << "[Lua] Workflow completed successfully." << std::endl;
  }
}

}  // namespace

void print_dependency_versions() {
  std::cout << "Third-party component versions:" << std::endl;

  int git_major = 0;
  int git_minor = 0;
  int git_revision = 0;
  git_libgit2_version(&git_major, &git_minor, &git_revision);
  std::cout << "  libgit2: " << git_major << '.' << git_minor << '.' << git_revision
            << std::endl;

  curl_version_info_data const *curl_info = curl_version_info(CURLVERSION_NOW);
  std::cout << "  libcurl: " << curl_info->version;
  std::vector<std::string> curl_features;
  if (curl_info->features & CURL_VERSION_ZSTD) {
    curl_features.push_back("zstd");
  }
  if (curl_info->features & CURL_VERSION_BROTLI) {
    curl_features.push_back("brotli");
  }
  if (curl_info->features & CURL_VERSION_LIBZ) {
    curl_features.push_back("zlib");
  }
  if (!curl_features.empty()) {
    std::cout << " (";
    for (size_t i = 0; i < curl_features.size(); ++i) {
      if (i > 0) std::cout << ", ";
      std::cout << curl_features[i];
    }
    std::cout << ")";
  }
  std::cout << std::endl;
  std::cout << "  libssh2: " << LIBSSH2_VERSION << std::endl;
  std::array<char, 32> mbedtls_version{};
  mbedtls_version_get_string_full(mbedtls_version.data());
  std::cout << "  mbedTLS: " << mbedtls_version.data() << std::endl;
  std::cout << "  libarchive: " << archive_version_details() << std::endl;
  std::cout << "  Lua: " << LUA_RELEASE << std::endl;
  std::cout << "  oneTBB: " << TBB_runtime_version() << std::endl;
  std::cout << "  BLAKE3: " << BLAKE3_VERSION_STRING << std::endl;
  std::cout << "  zlib: " << zlibVersion() << std::endl;
  std::cout << "  bzip2: " << BZ2_bzlibVersion() << std::endl;
  std::cout << "  zstd: " << ZSTD_versionString() << std::endl;
  std::cout << "  liblzma: " << lzma_version_string() << std::endl;
  std::cout << "  AWS SDK for C++: " << Aws::Version::GetVersionString() << std::endl;

  Aws::Crt::ApiHandle crt_handle;
  auto const crt_version = crt_handle.GetCrtVersion();
  std::cout << "  AWS CRT: " << crt_version.major << '.' << crt_version.minor << '.'
            << crt_version.patch << std::endl;

  std::cout << "  CLI11: " << CLI11_VERSION << std::endl;
}

int main(int argc, char **argv) {
  bool show_versions = false;
  std::vector<std::string_view> positional_args;
  positional_args.reserve(static_cast<size_t>(argc));

  for (int i = 1; i < argc; ++i) {
    std::string_view const arg{ argv[i] };
    if (arg == "-v") {
      show_versions = true;
    } else if (!arg.empty() && arg.front() == '-') {
      std::cerr << "Unknown option: " << arg << std::endl;
      std::cerr << "Usage: " << (argc > 0 ? argv[0] : "envy")
                << " [-v] s3://<bucket>/<key> [region]" << std::endl;
      return EXIT_FAILURE;
    } else {
      positional_args.push_back(arg);
    }
  }

  if (show_versions) {
    print_dependency_versions();
    return EXIT_SUCCESS;
  }

  if (positional_args.empty()) {
    std::cerr << "Usage: " << (argc > 0 ? argv[0] : "envy")
              << " [-v] s3://<bucket>/<key> [region]" << std::endl;
    return EXIT_FAILURE;
  }

  try {
    auto const parts = parse_s3_uri(positional_args.front());
    std::string const region_arg =
        positional_args.size() >= 2 ? std::string(positional_args[1]) : std::string{};

    std::mutex console_mutex;
    auto const workspace_root = std::filesystem::current_path();
    std::string const git_probe_url = "https://github.com/libgit2/libgit2.git";
    std::string const curl_probe_url = "https://www.example.com/";

    tbb::task_arena arena;
    arena.execute([&] {
      tbb::flow::graph graph;
      tbb::flow::broadcast_node<tbb::flow::continue_msg> kickoff(graph);

      auto make_task = [&](auto &&fn) {
        using Fn = std::decay_t<decltype(fn)>;
        Fn task_fn = std::forward<decltype(fn)>(fn);
        return tbb::flow::continue_node<tbb::flow::continue_msg>(
            graph,
            [task_fn = std::move(task_fn)](tbb::flow::continue_msg const &) mutable {
              task_fn();
            });
      };

      auto lua_task = make_task(
          [&] { run_lua_workflow(parts.bucket, parts.key, region_arg, console_mutex); });
      auto git_task = make_task(
          [&] { run_git_tls_probe(git_probe_url, workspace_root, console_mutex); });
      auto curl_task =
          make_task([&] { run_curl_tls_probe(curl_probe_url, console_mutex); });

      tbb::flow::make_edge(kickoff, lua_task);
      tbb::flow::make_edge(kickoff, git_task);
      tbb::flow::make_edge(kickoff, curl_task);

      kickoff.try_put(tbb::flow::continue_msg{});
      graph.wait_for_all();
    });
    return EXIT_SUCCESS;
  } catch (std::exception const &ex) {
    std::cerr << "Execution failed: " << ex.what() << '\n';
    return EXIT_FAILURE;
  }
}

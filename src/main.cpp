#include <archive.h>
#include <archive_entry.h>

#include <aws/core/Aws.h>
#include <aws/core/Version.h>
#include <aws/core/platform/Environment.h>
#include <aws/core/auth/AWSCredentialsProviderChain.h>
#include <aws/core/auth/SSOCredentialsProvider.h>
#include <aws/core/client/ClientConfiguration.h>
#include <aws/core/http/HttpTypes.h>
#include <aws/core/utils/logging/LogLevel.h>
#include <aws/core/utils/logging/NullLogSystem.h>
#include <aws/crt/Api.h>
#include <aws/s3/S3Client.h>
#include <aws/s3/model/GetObjectRequest.h>
#include <aws/s3/model/HeadObjectRequest.h>

#include <blake3.h>

#include "lua.hpp"

#include <curl/curl.h>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <libssh2.h>
#include <memory>
#include <mutex>
#include <mbedtls/sha256.h>
#include <mbedtls/version.h>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <string_view>
#include <vector>
#include <zlib.h>

#include <git2.h>
#include <oneapi/tbb/version.h>

#include <tbb/flow_graph.h>
#include <tbb/task_arena.h>

namespace {

std::string to_hex(const uint8_t *data, size_t length)
{
  std::ostringstream oss;
  oss << std::hex << std::setfill('0');
  for (size_t i = 0; i < length; ++i) {
    oss << std::setw(2) << static_cast<int>(data[i]);
  }
  return oss.str();
}

class AwsApiGuard {
 public:
  AwsApiGuard()
  {
    ::setenv("AWS_SDK_LOAD_CONFIG", "1", 1);
    options_.loggingOptions.logLevel = Aws::Utils::Logging::LogLevel::Off;
    options_.loggingOptions.logger_create_fn = []() {
      return Aws::MakeShared<Aws::Utils::Logging::NullLogSystem>("envy-cmake-test-logging");
    };
    Aws::InitAPI(options_);
  }

  AwsApiGuard(const AwsApiGuard &) = delete;
  AwsApiGuard &operator=(const AwsApiGuard &) = delete;

  ~AwsApiGuard()
  {
    Aws::ShutdownAPI(options_);
  }

 private:
  Aws::SDKOptions options_{};
};

std::filesystem::path create_temp_directory()
{
  const auto base = std::filesystem::temp_directory_path();
  if (!std::filesystem::exists(base)) {
    throw std::runtime_error("Temporary directory base does not exist");
  }

  std::random_device rd;
  std::mt19937_64 rng(rd());
  std::uniform_int_distribution<uint64_t> dist;

  std::ostringstream name;
  name << "envy-cmake-test-" << std::hex << std::setw(16) << std::setfill('0')
       << dist(rng);
  const auto candidate = base / name.str();

  std::error_code ec;
  std::filesystem::remove_all(candidate, ec);
  std::filesystem::create_directories(candidate);
  return candidate;
}

class TempResourceManager {
 public:
  TempResourceManager() = default;

  TempResourceManager(const TempResourceManager &) = delete;
  TempResourceManager &operator=(const TempResourceManager &) = delete;

  ~TempResourceManager()
  {
    cleanup();
  }

  std::filesystem::path create_directory()
  {
    auto dir = create_temp_directory();
    tracked_directories_.push_back(dir);
    return dir;
  }

  void cleanup() noexcept
  {
    for (auto it = tracked_directories_.rbegin(); it != tracked_directories_.rend(); ++it) {
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
  explicit TempManagerScope(TempResourceManager &manager) : manager_(manager)
  {
    g_temp_manager = &manager_;
  }

  TempManagerScope(const TempManagerScope &) = delete;
  TempManagerScope &operator=(const TempManagerScope &) = delete;

  ~TempManagerScope()
  {
    g_temp_manager = nullptr;
  }

 private:
  TempResourceManager &manager_;
};

struct S3UriParts {
  std::string bucket;
  std::string key;
};

S3UriParts parse_s3_uri(std::string_view uri)
{
  static constexpr std::string_view kScheme = "s3://";
  if (!uri.starts_with(kScheme)) {
    throw std::invalid_argument("S3 URI must start with s3://");
  }
  std::string_view remainder = uri.substr(kScheme.size());
  const auto slash = remainder.find('/');
  if (slash == std::string_view::npos || slash == 0 || slash + 1 >= remainder.size()) {
    throw std::invalid_argument("S3 URI must include bucket and key, e.g. s3://bucket/key");
  }
  S3UriParts parts;
  parts.bucket = std::string(remainder.substr(0, slash));
  parts.key = std::string(remainder.substr(slash + 1));
  return parts;
}

std::shared_ptr<Aws::Auth::AWSCredentialsProvider> select_credentials_provider(
    const Aws::S3::S3ClientConfiguration &s3_config)
{
  static constexpr const char *kAllocationTag = "envy-cmake-test-sso";
  const Aws::String profile_from_env = Aws::Environment::GetEnv("AWS_PROFILE");
  const Aws::String profile = profile_from_env.empty() ? Aws::Auth::GetConfigProfileName() : profile_from_env;
  try {
    auto client_config = Aws::MakeShared<Aws::Client::ClientConfiguration>(kAllocationTag);
    *client_config = static_cast<const Aws::Client::ClientConfiguration &>(s3_config);
    auto sso_provider = Aws::MakeShared<Aws::Auth::SSOCredentialsProvider>(
        kAllocationTag, profile, std::static_pointer_cast<const Aws::Client::ClientConfiguration>(client_config));
    std::cout << "[aws-sdk] Using AWS SSO credentials provider for profile '" << profile
              << "'." << std::endl;
    return sso_provider;
  } catch (const std::exception &ex) {
    std::cout << "[aws-sdk] AWS SSO provider initialization failed: " << ex.what()
              << "; falling back to default provider chain." << std::endl;
  }
  return Aws::MakeShared<Aws::Auth::DefaultAWSCredentialsProviderChain>(kAllocationTag);
}

void archive_copy_data(struct archive *source, struct archive *dest)
{
  const void *buff = nullptr;
  size_t size = 0;
  la_int64_t offset = 0;
  while (true) {
    const int r = archive_read_data_block(source, &buff, &size, &offset);
    if (r == ARCHIVE_EOF) {
      break;
    }
    if (r != ARCHIVE_OK) {
      throw std::runtime_error(std::string("libarchive read error: ") +
                               archive_error_string(source));
    }
    const int write_result = archive_write_data_block(dest, buff, size, offset);
    if (write_result != ARCHIVE_OK) {
      throw std::runtime_error(std::string("libarchive write error: ") +
                               archive_error_string(dest));
    }
  }
}

std::uint64_t extract_archive(const std::filesystem::path &archive_path,
                              const std::filesystem::path &destination)
{
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
    const std::string message = std::string("libarchive failed to open ") +
                                archive_path.string() + ": " +
                                archive_error_string(reader);
    archive_write_free(writer);
    archive_read_free(reader);
    throw std::runtime_error(message);
  }

  archive_entry *entry = nullptr;
  std::uint64_t regular_files = 0;
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

std::array<uint8_t, BLAKE3_OUT_LEN> compute_blake3_file(const std::filesystem::path &path)
{
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

constexpr size_t kSha256DigestLength = 32;

std::array<uint8_t, kSha256DigestLength> compute_sha256_file(const std::filesystem::path &path)
{
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("Failed to open " + path.string() + " for SHA256 hashing");
  }

  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  struct Sha256ContextGuard {
    mbedtls_sha256_context *context;
    ~Sha256ContextGuard()
    {
      if (context) {
        mbedtls_sha256_free(context);
      }
    }
  } guard{&ctx};
  if (mbedtls_sha256_starts(&ctx, /*is224=*/0) != 0) {
    throw std::runtime_error("mbedtls_sha256_starts failed");
  }

  std::array<char, 1 << 15> buffer{};
  while (input.read(buffer.data(), buffer.size()) || input.gcount() > 0) {
    const auto read_bytes = static_cast<size_t>(input.gcount());
    if (read_bytes > 0) {
      const unsigned char *data = reinterpret_cast<const unsigned char *>(buffer.data());
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

std::vector<std::filesystem::path> collect_first_regular_files(const std::filesystem::path &root,
                                                               std::size_t max_count)
{
  std::vector<std::filesystem::path> files;
  files.reserve(max_count);

  std::error_code ec;
  for (std::filesystem::recursive_directory_iterator it(
           root, std::filesystem::directory_options::follow_directory_symlink, ec), end;
       it != end && files.size() < max_count; it.increment(ec)) {
    if (ec) {
      throw std::runtime_error("Failed to iterate " + root.string() + ": " + ec.message());
    }
    std::error_code status_ec;
    const auto status = std::filesystem::status(it->path(), status_ec);
    if (status_ec) {
      throw std::runtime_error("Failed to query status for " + it->path().string() +
                               ": " + status_ec.message());
    }
    if (std::filesystem::is_regular_file(status)) {
      files.push_back(it->path());
    }
  }
  if (ec) {
    throw std::runtime_error("Failed to finalize iteration for " + root.string() +
                             ": " + ec.message());
  }
  return files;
}

std::string relative_display(const std::filesystem::path &path, const std::filesystem::path &base)
{
  std::error_code ec;
  const auto rel = std::filesystem::relative(path, base, ec);
  if (!ec) {
    const auto normalized = rel.lexically_normal();
    if (!normalized.empty()) {
      return normalized.generic_string();
    }
  }
  return path.filename().generic_string();
}

std::filesystem::path download_s3_object(TempResourceManager &manager,
                                         const std::string &bucket,
                                         const std::string &key,
                                         const std::string &region)
{
  if (bucket.empty()) {
    throw std::runtime_error("S3 bucket name must not be empty");
  }
  if (key.empty()) {
    throw std::runtime_error("S3 object key must not be empty");
  }

  const auto temp_dir = manager.create_directory();
  const auto file_name = std::filesystem::path(key).filename();
  if (file_name.empty()) {
    throw std::runtime_error("S3 object key does not contain a filename");
  }
  const auto destination = temp_dir / file_name;

  Aws::S3::S3ClientConfiguration config;
  if (!region.empty()) {
    config.region = region.c_str();
  }
  config.scheme = Aws::Http::Scheme::HTTPS;
  config.verifySSL = true;
  config.connectTimeoutMs = 3000;
  config.requestTimeoutMs = 30000;

  const auto credentials_provider = select_credentials_provider(config);
  Aws::S3::S3Client client(credentials_provider, nullptr, config);

  long long expected_size = -1;
  {
    Aws::S3::Model::HeadObjectRequest head_request;
    head_request.SetBucket(bucket.c_str());
    head_request.SetKey(key.c_str());
    const auto head_outcome = client.HeadObject(head_request);
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
  const auto progress_state = std::make_shared<ProgressState>();

  request.SetDataReceivedEventHandler([progress_state, expected_size](const Aws::Http::HttpRequest *,
                                                                      Aws::Http::HttpResponse *,
                                                                      long long bytes_transferred) {
    if (bytes_transferred <= 0) {
      return;
    }
    std::lock_guard<std::mutex> lock(progress_state->mutex);
    progress_state->downloaded += bytes_transferred;
    const auto now = std::chrono::steady_clock::now();
    if (now - progress_state->last_update < std::chrono::milliseconds(200)) {
      return;
    }
    progress_state->last_update = now;
    std::cout << '\r';
    if (expected_size > 0) {
      const double percent = static_cast<double>(progress_state->downloaded) * 100.0 /
                             static_cast<double>(expected_size);
      const double clamped = percent > 100.0 ? 100.0 : percent;
      std::cout << "[aws-sdk] Download progress: " << std::fixed << std::setprecision(1)
                << clamped << "%";
    } else {
      const double mebibytes = static_cast<double>(progress_state->downloaded) / (1024.0 * 1024.0);
      std::cout << "[aws-sdk] Downloaded " << std::fixed << std::setprecision(2) << mebibytes
                << " MiB";
    }
    std::cout << std::flush;
  });

  const auto download_start = std::chrono::steady_clock::now();
  auto outcome = client.GetObject(request);
  const auto download_end = std::chrono::steady_clock::now();

  {
    std::lock_guard<std::mutex> lock(progress_state->mutex);
    if (progress_state->downloaded > 0) {
      std::cout << '\r';
      if (expected_size > 0) {
        std::cout << "[aws-sdk] Download progress: 100.0%";
      } else {
        const double mebibytes = static_cast<double>(progress_state->downloaded) / (1024.0 * 1024.0);
        std::cout << "[aws-sdk] Downloaded " << std::fixed << std::setprecision(2) << mebibytes
                  << " MiB";
      }
      std::cout << std::string(10, ' ') << '\n';
    }
  }

  if (!outcome.IsSuccess()) {
    const auto &error = outcome.GetError();
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

  const auto sha256 = compute_sha256_file(destination);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(download_end - download_start);
  std::cout << "[aws-sdk] Downloaded s3://" << bucket << '/' << key << " to "
            << destination << " in " << std::fixed << std::setprecision(3)
            << static_cast<double>(elapsed.count()) / 1000.0 << "s" << std::endl;
  std::cout << "[aws-sdk] SHA256(" << destination.filename() << ") = "
            << to_hex(sha256.data(), sha256.size()) << std::endl;
  return destination;
}

int lua_download_s3_object(lua_State *L)
{
  const int argc = lua_gettop(L);
  if (argc < 2 || argc > 3) {
    return luaL_error(L, "download_s3_object expects bucket, key, [region]");
  }
  if (!g_temp_manager) {
    return luaL_error(L, "temporary resource manager is not initialized");
  }

  const std::string bucket = luaL_checkstring(L, 1);
  const std::string key = luaL_checkstring(L, 2);
  const std::string region = argc >= 3 ? luaL_checkstring(L, 3) : "";

  try {
    const auto archive_path = download_s3_object(*g_temp_manager, bucket, key, region);
    lua_pushstring(L, archive_path.string().c_str());
    return 1;
  } catch (const std::exception &ex) {
    return luaL_error(L, "download_s3_object failed: %s", ex.what());
  }
}

int lua_extract_to_temp(lua_State *L)
{
  const std::string archive = luaL_checkstring(L, 1);
  if (!g_temp_manager) {
    return luaL_error(L, "temporary resource manager is not initialized");
  }

  try {
    const auto destination = g_temp_manager->create_directory();
    const auto count = extract_archive(archive, destination);
    std::cout << "[lua] Extracted " << count << " files" << std::endl;

    const auto sample_files = collect_first_regular_files(destination, 5);
    if (sample_files.empty()) {
      std::cout << "[lua] No regular files discovered in archive." << std::endl;
    } else {
      std::size_t index = 1;
      for (const auto &file_path : sample_files) {
        const auto digest = compute_blake3_file(file_path);
        std::cout << "[lua] BLAKE3 sample " << index++ << ": "
                  << relative_display(file_path, destination)
                  << " => " << to_hex(digest.data(), digest.size()) << std::endl;
      }
    }

    lua_pushstring(L, destination.string().c_str());
    lua_pushinteger(L, static_cast<lua_Integer>(count));
    return 2;
  } catch (const std::exception &ex) {
    return luaL_error(L, "extract_to_temp failed: %s", ex.what());
  }
}

static constexpr char kLuaScript[] = R"(local bucket = assert(bucket, "bucket must be set")
local key = assert(key, "key must be set")
local region = region or ""

local archive_path = download_s3_object(bucket, key, region)
extract_to_temp(archive_path)
)";

void run_lua_workflow(const std::string &bucket,
                      const std::string &key,
                      const std::string &region)
{
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
    const char *message = lua_tostring(state.get(), -1);
    throw std::runtime_error(std::string("Failed to load Lua script: ") +
                             (message ? message : "unknown error"));
  }
  if (lua_pcall(state.get(), 0, LUA_MULTRET, 0) != LUA_OK) {
    const char *message = lua_tostring(state.get(), -1);
    throw std::runtime_error(std::string("Lua script execution failed: ") +
                             (message ? message : "unknown error"));
  }

  std::cout << "Lua workflow completed successfully." << std::endl;
}

}  // namespace

void print_dependency_versions()
{
  std::cout << "Third-party component versions:" << std::endl;

  int git_major = 0;
  int git_minor = 0;
  int git_revision = 0;
  git_libgit2_version(&git_major, &git_minor, &git_revision);
  std::cout << "  libgit2: " << git_major << '.' << git_minor << '.' << git_revision << std::endl;

  std::cout << "  libcurl: " << LIBCURL_VERSION << std::endl;
  std::cout << "  libssh2: " << LIBSSH2_VERSION << std::endl;
  std::array<char, 32> mbedtls_version{};
  mbedtls_version_get_string_full(mbedtls_version.data());
  std::cout << "  mbedTLS: " << mbedtls_version.data() << std::endl;
  std::cout << "  libarchive: " << archive_version_details() << std::endl;
  std::cout << "  Lua: " << LUA_RELEASE << std::endl;
  std::cout << "  oneTBB: " << TBB_runtime_version() << std::endl;
  std::cout << "  BLAKE3: " << BLAKE3_VERSION_STRING << std::endl;
  std::cout << "  zlib: " << zlibVersion() << std::endl;
  std::cout << "  AWS SDK for C++: " << Aws::Version::GetVersionString() << std::endl;

  Aws::Crt::ApiHandle crt_handle;
  const auto crt_version = crt_handle.GetCrtVersion();
  std::cout << "  AWS CRT: " << crt_version.major << '.' << crt_version.minor << '.'
            << crt_version.patch << std::endl;
}

int main(int argc, char **argv)
{
  bool show_versions = false;
  std::vector<std::string_view> positional_args;
  positional_args.reserve(static_cast<size_t>(argc));

  for (int i = 1; i < argc; ++i) {
    const std::string_view arg{argv[i]};
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
    const auto parts = parse_s3_uri(positional_args.front());
    const std::string region_arg = positional_args.size() >= 2 ? std::string(positional_args[1]) : std::string{};

    tbb::task_arena arena;
    arena.execute([&] {
      tbb::flow::graph graph;
      tbb::flow::continue_node<tbb::flow::continue_msg> runner(
          graph,
          [&](const tbb::flow::continue_msg &) {
            run_lua_workflow(parts.bucket, parts.key, region_arg);
          });

      runner.try_put(tbb::flow::continue_msg{});
      graph.wait_for_all();
    });
    return EXIT_SUCCESS;
  } catch (const std::exception &ex) {
    std::cerr << "Execution failed: " << ex.what() << '\n';
    return EXIT_FAILURE;
  }
}

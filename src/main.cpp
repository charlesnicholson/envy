#include <archive.h>
#include <archive_entry.h>

#include <aws/core/Aws.h>
#include <aws/core/client/ClientConfiguration.h>
#include <aws/core/http/HttpTypes.h>
#include <aws/core/utils/logging/LogLevel.h>
#include <aws/s3/S3Client.h>
#include <aws/s3/model/GetObjectRequest.h>

#include <blake3.h>

#include "lua.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

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
    options_.loggingOptions.logLevel = Aws::Utils::Logging::LogLevel::Error;
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
  name << "codex-cmake-test-" << std::hex << std::setw(16) << std::setfill('0')
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

void extract_archive(const std::filesystem::path &archive_path,
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

std::uint64_t count_regular_files(const std::filesystem::path &root)
{
  std::uint64_t count = 0;
  std::error_code ec;
  for (std::filesystem::recursive_directory_iterator it(
           root, std::filesystem::directory_options::follow_directory_symlink, ec), end;
       it != end; it.increment(ec)) {
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
      ++count;
    }
  }
  if (ec) {
    throw std::runtime_error("Failed to finalize iteration for " + root.string() +
                             ": " + ec.message());
  }
  return count;
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
  if (region.empty()) {
    throw std::runtime_error("S3 region must not be empty");
  }

  const auto temp_dir = manager.create_directory();
  const auto file_name = std::filesystem::path(key).filename();
  if (file_name.empty()) {
    throw std::runtime_error("S3 object key does not contain a filename");
  }
  const auto destination = temp_dir / file_name;

  Aws::Client::ClientConfiguration config;
  config.region = region.c_str();
  config.scheme = Aws::Http::Scheme::HTTPS;
  config.verifySSL = true;
  config.connectTimeoutMs = 3000;
  config.requestTimeoutMs = 30000;

  Aws::S3::S3Client client(config);

  Aws::S3::Model::GetObjectRequest request;
  request.SetBucket(bucket.c_str());
  request.SetKey(key.c_str());

  auto outcome = client.GetObject(request);
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

  std::cout << "[lua] Downloaded s3://" << bucket << '/' << key << " to "
            << destination << std::endl;
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
  const std::string region = argc >= 3 ? luaL_checkstring(L, 3) : "us-east-1";

  try {
    const auto archive_path = download_s3_object(*g_temp_manager, bucket, key, region);
    lua_pushstring(L, archive_path.string().c_str());
    return 1;
  } catch (const std::exception &ex) {
    return luaL_error(L, "download_s3_object failed: %s", ex.what());
  }
}

int lua_blake3_hex(lua_State *L)
{
  const std::string path = luaL_checkstring(L, 1);
  try {
    const auto digest = compute_blake3_file(path);
    const auto hex = to_hex(digest.data(), digest.size());
    std::cout << "[lua] BLAKE3(" << path << ") = " << hex << std::endl;
    lua_pushlstring(L, hex.c_str(), hex.size());
    return 1;
  } catch (const std::exception &ex) {
    return luaL_error(L, "blake3_hex failed: %s", ex.what());
  }
}

int lua_unzip_to_temp(lua_State *L)
{
  const std::string archive = luaL_checkstring(L, 1);
  if (!g_temp_manager) {
    return luaL_error(L, "temporary resource manager is not initialized");
  }

  try {
    const auto destination = g_temp_manager->create_directory();
    extract_archive(archive, destination);
    const auto count = count_regular_files(destination);
    std::cout << "[lua] Extracted " << count << " files into " << destination << std::endl;
    lua_pushstring(L, destination.string().c_str());
    lua_pushinteger(L, static_cast<lua_Integer>(count));
    return 2;
  } catch (const std::exception &ex) {
    return luaL_error(L, "unzip_to_temp failed: %s", ex.what());
  }
}

static constexpr char kLuaScript[] = R"(local bucket = assert(os.getenv("CODEX_S3_BUCKET"), "CODEX_S3_BUCKET must be set")
local key = assert(os.getenv("CODEX_S3_KEY"), "CODEX_S3_KEY must be set")
local region = os.getenv("CODEX_S3_REGION") or "us-east-1"

local archive_path = download_s3_object(bucket, key, region)
blake3_hex(archive_path)
unzip_to_temp(archive_path)
)";

void run_lua_workflow()
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
  lua_pushcfunction(state.get(), lua_blake3_hex);
  lua_setglobal(state.get(), "blake3_hex");
  lua_pushcfunction(state.get(), lua_unzip_to_temp);
  lua_setglobal(state.get(), "unzip_to_temp");

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

int main()
{
  try {
    tbb::task_arena arena;
    arena.execute([] {
      tbb::flow::graph graph;
      tbb::flow::continue_node<tbb::flow::continue_msg> runner(
          graph,
          [](const tbb::flow::continue_msg &) {
            run_lua_workflow();
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

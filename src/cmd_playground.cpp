#include "cmd_playground.h"

#include "extract.h"
#include "fetch.h"
#include "lua_util.h"
#include "sha256.h"
#include "tui.h"

#include "blake3.h"
#include "git2.h"
#include "tbb/flow_graph.h"
#include "tbb/task_arena.h"

extern "C" {
#include "lauxlib.h"
}

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace envy {
namespace {

template <typename Byte>
std::string to_hex(Byte const *data, size_t length) {
  static_assert(std::is_integral_v<Byte> && sizeof(Byte) == 1,
                "to_hex expects 1-byte integral values");

  static constexpr char kHexDigits[]{ "0123456789abcdef" };

  std::string hex(length * 2, '\0');
  for (size_t i{ 0 }; i < length; ++i) {
    auto const value{ static_cast<unsigned>(data[i]) };
    hex[2 * i] = kHexDigits[(value >> 4) & 0xF];
    hex[2 * i + 1] = kHexDigits[value & 0xF];
  }
  return hex;
}

std::filesystem::path create_temp_directory() {
  auto const base{ std::filesystem::temp_directory_path() };
  if (!std::filesystem::exists(base)) {
    throw std::runtime_error("Temporary directory base does not exist");
  }

  std::random_device rd;
  std::mt19937_64 rng{ rd() };
  std::uniform_int_distribution<uint64_t> dist;

  std::ostringstream name;
  name << "envy-playground-" << std::hex << std::setw(16) << std::setfill('0')
       << dist(rng);
  auto const candidate{ base / name.str() };

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
  ~TempResourceManager() { cleanup(); }

  std::filesystem::path create_directory() {
    auto dir{ create_temp_directory() };
    tracked_directories_.push_back(dir);
    return dir;
  }

  void cleanup() noexcept {
    for (auto it{ tracked_directories_.rbegin() }; it != tracked_directories_.rend();
         ++it) {
      std::error_code ec;
      std::filesystem::remove_all(*it, ec);
      if (ec) {
        auto const path_str{ it->string() };
        auto const message{ ec.message() };
        tui::warn("[cleanup] Failed to remove %s: %s", path_str.c_str(), message.c_str());
      }
    }
    tracked_directories_.clear();
  }

 private:
  std::vector<std::filesystem::path> tracked_directories_{};
};

TempResourceManager *g_temp_manager{ nullptr };

class TempManagerScope {
 public:
  explicit TempManagerScope(TempResourceManager &manager) : manager_{ manager } {
    g_temp_manager = &manager_;
  }

  TempManagerScope(TempManagerScope const &) = delete;
  TempManagerScope &operator=(TempManagerScope const &) = delete;
  ~TempManagerScope() { g_temp_manager = nullptr; }

 private:
  TempResourceManager &manager_;
};

std::array<uint8_t, BLAKE3_OUT_LEN> compute_blake3_file(
    std::filesystem::path const &path) {
  std::ifstream input{ path, std::ios::binary };
  if (!input) {
    throw std::runtime_error("Failed to open " + path.string() + " for BLAKE3 hashing");
  }

  blake3_hasher hasher;
  blake3_hasher_init(&hasher);

  std::array<char, 1 << 15> buffer{};
  while (input.read(buffer.data(), buffer.size()) || input.gcount() > 0) {
    auto const read_bytes{ static_cast<size_t>(input.gcount()) };
    if (read_bytes > 0) { blake3_hasher_update(&hasher, buffer.data(), read_bytes); }
  }
  if (input.bad()) {
    throw std::runtime_error("Failed while reading " + path.string() + " for BLAKE3");
  }

  std::array<uint8_t, BLAKE3_OUT_LEN> digest{};
  blake3_hasher_finalize(&hasher, digest.data(), digest.size());
  return digest;
}

std::vector<std::filesystem::path> collect_first_regular_files(
    std::filesystem::path const &root,
    std::size_t max_count) {
  std::vector<std::filesystem::path> files;
  files.reserve(max_count);

  std::error_code ec;
  for (std::filesystem::recursive_directory_iterator
           it{ root, std::filesystem::directory_options::follow_directory_symlink, ec },
       end;
       it != end && files.size() < max_count;
       it.increment(ec)) {
    if (ec) {
      throw std::runtime_error("Failed to iterate " + root.string() + ": " + ec.message());
    }
    std::error_code status_ec;
    auto const status{ std::filesystem::status(it->path(), status_ec) };
    if (status_ec) {
      throw std::runtime_error("Failed to query status for " + it->path().string() + ": " +
                               status_ec.message());
    }
    if (std::filesystem::is_regular_file(status)) { files.push_back(it->path()); }
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
  auto const rel{ std::filesystem::relative(path, base, ec) };
  if (!ec) {
    auto const normalized{ rel.lexically_normal() };
    if (!normalized.empty()) { return normalized.generic_string(); }
  }
  return path.filename().generic_string();
}

std::string infer_download_name(std::string_view uri) {
  auto const query_pos{ uri.find_first_of("?#") };
  std::string_view trimmed{ query_pos == std::string_view::npos
                                ? uri
                                : uri.substr(0, query_pos) };

  auto const last_sep{ trimmed.find_last_of("/\\") };
  std::string_view tail{ last_sep == std::string_view::npos
                             ? trimmed
                             : trimmed.substr(last_sep + 1) };

  if (tail.empty() || tail == "." || tail == "..") { return "download"; }
  return std::string{ tail };
}

std::filesystem::path download_resource(TempResourceManager &manager,
                                        std::string const &uri,
                                        std::string const &region) {
  if (uri.empty()) { throw std::runtime_error("Download URI must not be empty"); }

  auto const temp_dir{ manager.create_directory() };
  auto const file_name{ infer_download_name(uri) };
  auto destination{ temp_dir / std::filesystem::path{ file_name } };

  struct progress_state {
    std::mutex mutex;
    std::chrono::steady_clock::time_point last_emit{ std::chrono::steady_clock::now() };
    std::uint64_t transferred{ 0 };
    std::optional<std::uint64_t> total;
  };
  auto const state{ std::make_shared<progress_state>() };

  auto const start_time{ std::chrono::steady_clock::now() };

  auto progress_cb = [state](fetch_progress_t const &payload) -> bool {
    auto const *transfer = std::get_if<fetch_transfer_progress>(&payload);
    if (!transfer) { return true; }

    std::lock_guard<std::mutex> lock{ state->mutex };
    state->transferred = transfer->transferred;
    state->total = transfer->total;

    auto const now{ std::chrono::steady_clock::now() };
    if (now - state->last_emit < std::chrono::milliseconds(200)) { return true; }
    state->last_emit = now;

    if (transfer->total && *transfer->total > 0) {
      double const percent{ static_cast<double>(transfer->transferred) * 100.0 /
                            static_cast<double>(*transfer->total) };
      double const clamped{ percent > 100.0 ? 100.0 : percent };
      tui::info("[fetch] Download progress: %.1f%%", clamped);
    } else {
      double const mebibytes{ static_cast<double>(transfer->transferred) /
                              (1024.0 * 1024.0) };
      tui::info("[fetch] Downloaded %.2f MiB", mebibytes);
    }
    return true;
  };

  fetch_request request{ .source = uri,
                         .destination = destination,
                         .file_root = std::nullopt,
                         .progress = progress_cb };
  if (!region.empty()) { request.region = region; }

  auto const result{ fetch(request) };

  {
    std::lock_guard<std::mutex> lock{ state->mutex };
    if (state->transferred > 0) {
      if (state->total && *state->total > 0) {
        tui::info("[fetch] Download progress: 100.0%%");
      } else {
        double const mebibytes{ static_cast<double>(state->transferred) /
                                (1024.0 * 1024.0) };
        tui::info("[fetch] Downloaded %.2f MiB", mebibytes);
      }
    }
  }

  auto const elapsed{ std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start_time) };

  auto const sha256_digest{ sha256(result.resolved_destination) };
  double const elapsed_seconds{ static_cast<double>(elapsed.count()) / 1000.0 };
  tui::info("[fetch] Downloaded %s to %s in %.3fs",
            uri.c_str(),
            result.resolved_destination.string().c_str(),
            elapsed_seconds);
  auto const digest_hex = to_hex(sha256_digest.data(), sha256_digest.size());
  auto const filename_str{ result.resolved_destination.filename().string() };
  tui::info("[fetch] SHA256(%s) = %s", filename_str.c_str(), digest_hex.c_str());

  return result.resolved_destination;
}

int lua_download_resource(lua_State *L) {
  int const argc{ lua_gettop(L) };
  if (argc < 1 || argc > 2) {
    return luaL_error(L, "download_resource expects uri[, region]");
  }
  if (!g_temp_manager) {
    return luaL_error(L, "temporary resource manager is not initialized");
  }

  std::string const uri{ luaL_checkstring(L, 1) };
  std::string const region{ (argc >= 2 && !lua_isnil(L, 2)) ? luaL_checkstring(L, 2)
                                                            : "" };

  try {
    auto const archive_path{ download_resource(*g_temp_manager, uri, region) };
    lua_pushstring(L, archive_path.string().c_str());
    return 1;
  } catch (std::exception const &ex) {
    return luaL_error(L, "download_resource failed: %s", ex.what());
  }
}

int lua_extract_to_temp(lua_State *L) {
  std::string const archive{ luaL_checkstring(L, 1) };
  if (!g_temp_manager) {
    return luaL_error(L, "temporary resource manager is not initialized");
  }

  try {
    auto const destination{ g_temp_manager->create_directory() };
    auto const count{ extract(archive, destination) };
    tui::info("[lua] Extracted %llu files", static_cast<unsigned long long>(count));

    auto const sample_files{ collect_first_regular_files(destination, 5) };
    if (sample_files.empty()) {
      tui::info("[lua] No regular files discovered in archive.");
    } else {
      std::size_t index{ 1 };
      for (auto const &file_path : sample_files) {
        auto const digest{ compute_blake3_file(file_path) };
        auto const relative{ relative_display(file_path, destination) };
        auto const digest_hex{ to_hex(digest.data(), digest.size()) };
        auto const sample_index{ static_cast<unsigned long long>(index++) };
        tui::info("[lua] BLAKE3 sample %llu: %s => %s",
                  sample_index,
                  relative.c_str(),
                  digest_hex.c_str());
      }
    }

    lua_pushstring(L, destination.string().c_str());
    lua_pushinteger(L, static_cast<lua_Integer>(count));
    return 2;
  } catch (std::exception const &ex) {
    return luaL_error(L, "extract_to_temp failed: %s", ex.what());
  }
}

static constexpr char kLuaScript[]{ R"(local uri = assert(uri, "uri must be set")
local region = region or ""

local archive_path = download_resource(uri, region)
extract_to_temp(archive_path)
)" };

std::string format_git_error(int error_code) {
  git_error const *error{ git_error_last() };
  std::ostringstream oss;
  oss << "libgit2 error (" << error_code << ')';
  if (error && error->message) { oss << ": " << error->message; }
  return oss.str();
}

void run_git_tls_probe(std::string const &url,
                       std::filesystem::path const &workspace_root,
                       std::mutex &console_mutex) {
  git_libgit2_init();
  git_repository *repo{ nullptr };
  git_remote *remote{ nullptr };
  auto const probe_dir{ workspace_root / "out" / "cache" / "git_tls_probe" };
  std::error_code fs_ec;
  std::filesystem::remove_all(probe_dir, fs_ec);
  std::filesystem::create_directories(probe_dir, fs_ec);

  try {
    git_repository_init_options opts = GIT_REPOSITORY_INIT_OPTIONS_INIT;
    opts.flags = GIT_REPOSITORY_INIT_BARE;
    int const init_result{
      git_repository_init_ext(&repo, probe_dir.string().c_str(), &opts)
    };
    if (init_result != 0) { throw std::runtime_error(format_git_error(init_result)); }

    if (git_remote_lookup(&remote, repo, "origin") == 0) {
      git_remote_delete(repo, "origin");
      git_remote_free(remote);
      remote = nullptr;
    }

    int const create_result{ git_remote_create(&remote, repo, "origin", url.c_str()) };
    if (create_result != 0) { throw std::runtime_error(format_git_error(create_result)); }

    int const connect_result{
      git_remote_connect(remote, GIT_DIRECTION_FETCH, nullptr, nullptr, nullptr)
    };
    if (connect_result != 0) {
      throw std::runtime_error(format_git_error(connect_result));
    }

    git_remote_head const **heads{ nullptr };
    size_t head_count{ 0 };
    int const ls_result{ git_remote_ls(&heads, &head_count, remote) };
    if (ls_result != 0) { throw std::runtime_error(format_git_error(ls_result)); }

    {
      std::lock_guard<std::mutex> lock{ console_mutex };
      tui::info("[libgit2] Connected to %s and enumerated %llu refs",
                url.c_str(),
                static_cast<unsigned long long>(head_count));
      if (head_count > 0 && heads[0] && heads[0]->name) {
        tui::info("  First ref: %s", heads[0]->name);
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

void run_fetch_tls_probe(std::string const &url,
                         std::filesystem::path const &workspace_root,
                         std::mutex &console_mutex) {
  auto const probe_dir{ workspace_root / "out" / "cache" / "fetch_tls_probe" };
  std::error_code probe_ec;
  std::filesystem::create_directories(probe_dir, probe_ec);
  if (probe_ec) {
    throw std::runtime_error("Failed to create fetch probe directory: " +
                             probe_ec.message());
  }

  auto const destination{ probe_dir / "probe.bin" };

  try {
    auto const result{ fetch(fetch_request{ .source = url,
                                            .destination = destination,
                                            .file_root = std::nullopt,
                                            .progress = {} }) };
    std::uintmax_t bytes{ 0 };
    std::error_code size_ec;
    if (auto const size = std::filesystem::file_size(result.resolved_destination, size_ec);
        !size_ec) {
      bytes = size;
    }

    {
      std::lock_guard<std::mutex> lock{ console_mutex };
      tui::info("[fetch] Downloaded %llu bytes from %s",
                static_cast<unsigned long long>(bytes),
                url.c_str());
    }
  } catch (...) {
    std::error_code cleanup_ec;
    std::filesystem::remove(destination, cleanup_ec);
    std::filesystem::remove_all(probe_dir, cleanup_ec);
    throw;
  }

  std::error_code cleanup_ec;
  std::filesystem::remove(destination, cleanup_ec);
  std::filesystem::remove_all(probe_dir, cleanup_ec);
}

void run_lua_workflow(std::string const &uri,
                      std::string const &region,
                      std::mutex &console_mutex) {
  TempResourceManager temp_manager;
  TempManagerScope manager_scope{ temp_manager };

  auto state{ lua_make() };
  if (!state) { throw std::runtime_error("lua_make returned null"); }

  lua_add_envy(state);

  lua_pushcfunction(state.get(), lua_download_resource);
  lua_setglobal(state.get(), "download_resource");
  lua_pushcfunction(state.get(), lua_extract_to_temp);
  lua_setglobal(state.get(), "extract_to_temp");

  lua_pushlstring(state.get(), uri.c_str(), static_cast<lua_Integer>(uri.size()));
  lua_setglobal(state.get(), "uri");
  lua_pushlstring(state.get(), region.c_str(), static_cast<lua_Integer>(region.size()));
  lua_setglobal(state.get(), "region");

  if (!lua_run_string(state, kLuaScript)) {
    throw std::runtime_error("Lua script execution failed");
  }

  {
    std::lock_guard<std::mutex> lock{ console_mutex };
    tui::info("[Lua] Workflow completed successfully.");
  }
}

}  // anonymous namespace

cmd_playground::cmd_playground(cmd_playground::cfg cfg) : cfg_{ std::move(cfg) } {}

bool cmd_playground::execute() {
  if (cfg_.uri.empty()) { throw std::runtime_error("Playground URI must not be empty"); }

  // Local state for parallel tasks
  std::mutex console_mutex;
  auto const workspace_root{ std::filesystem::current_path() };
  std::string const git_probe_url{ "https://github.com/libgit2/libgit2.git" };
  std::string const curl_probe_url{ "https://www.example.com/" };
  std::string const source_uri{ cfg_.uri };

  // Create local graph for parallel execution
  tbb::flow::graph g;

  tbb::flow::broadcast_node<tbb::flow::continue_msg> kickoff{ g };

  tbb::flow::continue_node<tbb::flow::continue_msg> lua_task{
    g,
    [&](tbb::flow::continue_msg const &) {
      run_lua_workflow(source_uri, cfg_.region, console_mutex);
    }
  };

  tbb::flow::continue_node<tbb::flow::continue_msg> git_task{
    g,
    [&](tbb::flow::continue_msg const &) {
      run_git_tls_probe(git_probe_url, workspace_root, console_mutex);
    }
  };

  tbb::flow::continue_node<tbb::flow::continue_msg> curl_task{
    g,
    [&](tbb::flow::continue_msg const &) {
      run_fetch_tls_probe(curl_probe_url, workspace_root, console_mutex);
    }
  };

  tbb::flow::make_edge(kickoff, lua_task);
  tbb::flow::make_edge(kickoff, git_task);
  tbb::flow::make_edge(kickoff, curl_task);

  kickoff.try_put(tbb::flow::continue_msg{});
  g.wait_for_all();

  return true;
}

}  // namespace envy

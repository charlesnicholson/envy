#include "reexec.h"

#include "cache.h"
#include "cmd.h"
#include "envy_release.h"
#include "extract.h"
#include "fetch.h"
#include "platform.h"
#include "tui.h"

#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef ENVY_VERSION_STR
#error "ENVY_VERSION_STR must be defined by the build system"
#endif

namespace envy {

namespace {

char **g_argv{};

std::string_view get_self_version() {
  if (auto const *v = std::getenv("ENVY_TEST_SELF_VERSION")) { return v; }
  return ENVY_VERSION_STR;
}

void make_executable([[maybe_unused]] std::filesystem::path const &path) {
#ifndef _WIN32
  std::error_code ec;
  std::filesystem::permissions(path,
                               std::filesystem::perms::owner_exec |
                                   std::filesystem::perms::group_exec |
                                   std::filesystem::perms::others_exec,
                               std::filesystem::perm_options::add,
                               ec);
  if (ec) {
    tui::warn("reexec: failed to set executable permissions: %s", ec.message().c_str());
  }
#endif
}

void remove_quarantine([[maybe_unused]] std::filesystem::path const &path) {
#ifdef __APPLE__
  std::ostringstream cmd;
  cmd << "xattr -d com.apple.quarantine '" << path.string() << "' 2>/dev/null";
  std::system(cmd.str().c_str());
#endif
}

// Build child env: copy current env, add ENVY_REEXEC=1, strip ENVY_TEST_SELF_VERSION.
std::vector<std::string> build_child_env() {
  auto env{ platform::get_environment() };
  std::vector<std::string> result;
  result.reserve(env.size() + 1);
  bool found_reexec{ false };

  for (auto &entry : env) {
    if (entry.starts_with("ENVY_TEST_SELF_VERSION=")) { continue; }
    if (entry.starts_with("ENVY_REEXEC=")) {
      found_reexec = true;
      result.emplace_back("ENVY_REEXEC=1");
    } else {
      result.push_back(std::move(entry));
    }
  }

  if (!found_reexec) { result.emplace_back("ENVY_REEXEC=1"); }
  return result;
}

[[noreturn]] void do_reexec(std::filesystem::path const &binary) {
  tui::info("reexec: switching to envy at %s", binary.string().c_str());
  throw subprocess_exit{ platform::exec_process(binary, g_argv, build_child_env()) };
}

}  // namespace

void reexec_init(char **argv) { g_argv = argv; }

reexec_decision reexec_should(std::string_view self_version,
                              std::optional<std::string> const &requested_version,
                              bool reexec_env_set,
                              bool no_reexec_env_set) {
  if (!requested_version) { return reexec_decision::PROCEED; }
  if (no_reexec_env_set) { return reexec_decision::PROCEED; }
  if (self_version == "0.0.0") { return reexec_decision::PROCEED; }
  if (reexec_env_set) { return reexec_decision::PROCEED; }
  if (self_version == *requested_version) { return reexec_decision::PROCEED; }
  return reexec_decision::REEXEC;
}

void reexec_if_needed(envy_meta const &meta,
                      std::optional<std::filesystem::path> const &cli_cache_root) {
  // Consume and unset the loop guard if present
  bool const reexec_env_set{ std::getenv("ENVY_REEXEC") != nullptr };
  if (reexec_env_set) { platform::env_var_unset("ENVY_REEXEC"); }

  bool const no_reexec_env_set{ std::getenv("ENVY_NO_REEXEC") != nullptr };
  auto const self_ver{ get_self_version() };

  if (reexec_should(self_ver, meta.version, reexec_env_set, no_reexec_env_set) ==
      reexec_decision::PROCEED) {
    return;
  }

  auto const &version{ *meta.version };

  if (!envy_release_version_is_valid(version)) {
    throw std::runtime_error("reexec: invalid version string: " + version);
  }

  // Fast path: check if the requested version is already in cache
  auto const cache_root{ resolve_cache_root(cli_cache_root, meta.cache_for_platform()) };
  auto const cached_binary{ cache_root / "envy" / version / platform::exe_name("envy") };
  if (std::filesystem::exists(cached_binary)) { do_reexec(cached_binary); }

  // Slow path: download to temp dir, re-exec from there.
  // The re-exec'd binary's own cache::ensure_envy() will install itself into cache.

  std::string_view mirror{ kEnvyReleaseDownloadUrl };
  if (char const *env_mirror = std::getenv("ENVY_MIRROR"); env_mirror) {
    mirror = env_mirror;
  } else if (meta.mirror) {
    mirror = *meta.mirror;
  }

  auto const url{
    envy_release_url(mirror, version, platform::os_name(), platform::arch_name())
  };
  tui::info("reexec: downloading envy %s from %s", version.c_str(), url.c_str());

  auto const pid{ platform::get_process_id() };
  auto const tmp_dir{ std::filesystem::temp_directory_path() /
                      ("envy-reexec-" + version + "-" + std::to_string(pid)) };
  std::filesystem::create_directories(tmp_dir);

  auto const archive_path{
    tmp_dir / envy_release_archive_name(platform::os_name(), platform::arch_name())
  };

  auto const results{ fetch({ fetch_request_from_url(url, archive_path) }) };
  if (results.empty()) {
    throw std::runtime_error("reexec: failed to download envy " + version + " from " +
                             url + ": unknown error");
  }
  if (std::holds_alternative<std::string>(results[0])) {
    throw std::runtime_error("reexec: failed to download envy " + version + " from " +
                             url + ": " + std::get<std::string>(results[0]));
  }

  extract(archive_path, tmp_dir);

  std::error_code ec;
  std::filesystem::remove(archive_path, ec);

  auto const binary_path{ tmp_dir / platform::exe_name("envy") };
  if (!std::filesystem::exists(binary_path)) {
    throw std::runtime_error("reexec: archive did not contain expected binary: " +
                             binary_path.string());
  }

  make_executable(binary_path);
  remove_quarantine(binary_path);

  do_reexec(binary_path);
}

}  // namespace envy

#include "cache.h"

#include "embedded_init_resources.h"
#include "platform.h"
#include "shell_hooks.h"
#include "trace.h"
#include "tui.h"

#include <chrono>
#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <system_error>

#ifndef ENVY_VERSION_STR
#error "ENVY_VERSION_STR must be defined by the build system"
#endif

using path = std::filesystem::path;

namespace envy {

namespace {

bool copy_binary(path const &src, path const &dst) {
  std::error_code ec;
  std::filesystem::copy_file(src,
                             dst,
                             std::filesystem::copy_options::overwrite_existing,
                             ec);
  if (ec) {
    tui::warn("cache: failed to copy binary: %s", ec.message().c_str());
    return false;
  }

#ifndef _WIN32
  std::filesystem::permissions(dst,
                               std::filesystem::perms::owner_exec |
                                   std::filesystem::perms::group_exec |
                                   std::filesystem::perms::others_exec,
                               std::filesystem::perm_options::add,
                               ec);
  if (ec) {
    tui::warn("cache: failed to set executable permissions: %s", ec.message().c_str());
  }
#endif

  return true;
}

}  // namespace

path resolve_cache_root(std::optional<path> const &cli_override,
                        std::optional<std::string> const &manifest_cache) {
  if (cli_override) { return *cli_override; }
  if (char const *env{ std::getenv("ENVY_CACHE_ROOT") }) { return env; }
  if (manifest_cache) { return platform::expand_path(*manifest_cache); }
  if (auto def{ platform::get_default_cache_root() }) { return *def; }

  throw std::runtime_error("cannot determine cache root");
}

std::unique_ptr<cache> cache::ensure(std::optional<path> const &cli_cache_root,
                                     std::optional<std::string> const &manifest_cache) {
  auto const root{ resolve_cache_root(cli_cache_root, manifest_cache) };
  auto c{ std::make_unique<cache>(root) };

  try {
    std::string_view const types{ reinterpret_cast<char const *>(
                                      embedded::kTypeDefinitions),
                                  embedded::kTypeDefinitionsSize };
    c->ensure_envy(ENVY_VERSION_STR, platform::get_exe_path(), types);
  } catch (std::exception const &e) {
    tui::warn("cache: self-deploy failed: %s", e.what());
  }

  return c;
}

struct cache_impl {
  path root_;

  path specs_dir() const { return root_ / "specs"; }
  path packages_dir() const { return root_ / "packages"; }
  path locks_dir() const { return root_ / "locks"; }
};

struct cache::impl : cache_impl {};

struct cache::scoped_entry_lock::impl {
  path entry_dir_;
  platform::file_lock lock_;
  path lock_path_;
  std::string pkg_identity_;
  std::chrono::steady_clock::time_point lock_acquired_time{};
  bool completed_{ false };
  bool user_managed_{ false };

  impl(path entry_dir,
       platform::file_lock lock,
       path lock_path,
       std::string pkg_identity,
       std::chrono::steady_clock::time_point lock_acquired_at)
      : entry_dir_{ std::move(entry_dir) },
        lock_{ std::move(lock) },
        lock_path_{ std::move(lock_path) },
        pkg_identity_{ std::move(pkg_identity) },
        lock_acquired_time{ lock_acquired_at } {}

  path pkg_dir() const { return entry_dir_ / "pkg"; }
};

}  // namespace envy

namespace {

void remove_all_noexcept(path const &target) {
  if (auto ec{ envy::platform::remove_all_with_retry(target) }) {
    envy::tui::error("Failed to remove %s: %s",
                     target.string().c_str(),
                     ec.message().c_str());
  }
}

envy::cache::ensure_result ensure_entry(envy::cache_impl &impl,
                                        path const &entry_dir,
                                        path const &lock_path,
                                        std::string_view pkg_identity,
                                        std::string_view cache_key) {
  envy::cache::ensure_result result{ entry_dir, entry_dir / "pkg", nullptr };

  ENVY_TRACE_CACHE_CHECK_ENTRY(std::string(pkg_identity),
                               entry_dir.string(),
                               "before_lock");
  bool const complete_before_lock{ envy::cache::is_entry_complete(entry_dir) };
  ENVY_TRACE_CACHE_CHECK_RESULT(std::string(pkg_identity),
                                entry_dir.string(),
                                complete_before_lock,
                                "before_lock");

  if (complete_before_lock) {
    ENVY_TRACE_CACHE_HIT(std::string(pkg_identity),
                         std::string(cache_key),
                         result.pkg_path.string(),
                         true);
    return result;
  }

  std::filesystem::create_directories(impl.locks_dir());
  std::filesystem::create_directories(entry_dir);

  auto const lock_wait_start{ std::chrono::steady_clock::now() };
  envy::platform::file_lock lock{ lock_path };
  auto const wait_duration_ms{ std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now() - lock_wait_start)
                                   .count() };
  ENVY_TRACE_LOCK_ACQUIRED(std::string(pkg_identity),
                           lock_path.string(),
                           static_cast<std::int64_t>(wait_duration_ms));

  ENVY_TRACE_CACHE_CHECK_ENTRY(std::string(pkg_identity),
                               entry_dir.string(),
                               "after_lock");
  bool const complete_after_lock{ envy::cache::is_entry_complete(entry_dir) };
  ENVY_TRACE_CACHE_CHECK_RESULT(std::string(pkg_identity),
                                entry_dir.string(),
                                complete_after_lock,
                                "after_lock");

  if (complete_after_lock) {
    ENVY_TRACE_CACHE_HIT(std::string(pkg_identity),
                         std::string(cache_key),
                         result.pkg_path.string(),
                         false);
    return result;
  }

  ENVY_TRACE_CACHE_MISS(std::string(pkg_identity), std::string(cache_key));
  auto const lock_acquired_at{ std::chrono::steady_clock::now() };
  result.lock = envy::cache::scoped_entry_lock::make(entry_dir,
                                                     std::move(lock),
                                                     lock_path,
                                                     std::string{ pkg_identity },
                                                     lock_acquired_at);
  return result;
}

}  // namespace

namespace envy {

cache::scoped_entry_lock::scoped_entry_lock(
    path entry_dir,
    platform::file_lock lock,
    path lock_path,
    std::string pkg_identity,
    std::chrono::steady_clock::time_point lock_acquired_at)
    : m{ std::make_unique<impl>(std::move(entry_dir),
                                std::move(lock),
                                std::move(lock_path),
                                std::move(pkg_identity),
                                lock_acquired_at) } {
  tui::debug("scoped_entry_lock CTOR: entry_dir=%s", m->entry_dir_.string().c_str());
  tui::debug("  about to remove_all(install_dir)");
  remove_all_noexcept(install_dir());
  tui::debug("  about to remove_all(work_dir)");
  remove_all_noexcept(work_dir());  // always delete (purely ephemeral)

  // Preserve fetch/ to enable per-file caching across failed attempts

  // Ensure directories exist
  tui::debug("  creating directories");
  std::filesystem::create_directories(fetch_dir());
  std::filesystem::create_directories(install_dir());
  std::filesystem::create_directories(stage_dir());
  std::filesystem::create_directories(tmp_dir());
  tui::debug("scoped_entry_lock CTOR: done");
}

cache::scoped_entry_lock::~scoped_entry_lock() {
  auto const hold_duration_ms{ std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now() -
                                   m->lock_acquired_time)
                                   .count() };
  tui::debug("scoped_entry_lock DTOR: entry_dir=%s completed=%s user_managed=%s",
             m->entry_dir_.string().c_str(),
             m->completed_ ? "true" : "false",
             m->user_managed_ ? "true" : "false");

  if (m->completed_) {
    tui::debug("  DTOR: SUCCESS PATH - renaming install_dir -> pkg_dir");
    remove_all_noexcept(m->pkg_dir());
    tui::debug("    from: %s", install_dir().string().c_str());
    tui::debug("    to:   %s", m->pkg_dir().string().c_str());
    platform::atomic_rename(install_dir(), m->pkg_dir());
    tui::debug("  DTOR: cleaning up work/fetch dirs");
    remove_all_noexcept(work_dir());
    remove_all_noexcept(fetch_dir());
    tui::debug("  DTOR: touching envy-complete");
    platform::touch_file(m->entry_dir_ / "envy-complete");
    platform::flush_directory(m->entry_dir_);
    tui::debug("  DTOR: completed path success");
  } else if (m->user_managed_) {
    tui::debug("  DTOR: USER-MANAGED PATH - purging entire entry_dir");
    remove_all_noexcept(m->entry_dir_);
    tui::debug("  DTOR: user-managed purge complete");
  } else {
    tui::debug("  DTOR: CACHE-MANAGED FAILURE PATH - cleaning up");
    // Check empty install_dir AND fetch_dir (installation didn't use cache at all)
    std::error_code ec;

    bool const install_dir_empty{ [&] {  // Check install_dir
      std::filesystem::directory_iterator it{ install_dir(), ec };
      if (ec) {
        ec.clear();
        return false;  // Conservative assumption: treat as not empty if error
      }
      return it == std::filesystem::directory_iterator{};
    }() };

    bool const fetch_dir_empty{ [&] {  // Check fetch_dir
      std::filesystem::directory_iterator it{ fetch_dir(), ec };
      if (ec) {
        ec.clear();
        return false;  // Conservative assumption: treat as not empty if error
      }
      return it == std::filesystem::directory_iterator{};
    }() };

    tui::debug("  DTOR: install_empty=%s fetch_empty=%s",
               install_dir_empty ? "true" : "false",
               fetch_dir_empty ? "true" : "false");

    remove_all_noexcept(install_dir());
    remove_all_noexcept(work_dir());

    if (install_dir_empty && fetch_dir_empty) {
      tui::debug("  DTOR: both empty, wiping entry");
      remove_all_noexcept(fetch_dir());
      remove_all_noexcept(m->pkg_dir());
    }
  }

  tui::debug(
      "  DTOR: lock will be released and lock file deleted by file_lock destructor");
  ENVY_TRACE_LOCK_RELEASED(m->pkg_identity_,
                           m->lock_path_.string(),
                           static_cast<std::int64_t>(hold_duration_ms));
  tui::debug("scoped_entry_lock DTOR: done");
}

cache::scoped_entry_lock::ptr_t cache::scoped_entry_lock::make(
    path entry_dir,
    platform::file_lock lock_handle,
    path lock_path,
    std::string pkg_identity,
    std::chrono::steady_clock::time_point lock_acquired_at) {
  return ptr_t{ new scoped_entry_lock{ std::move(entry_dir),
                                       std::move(lock_handle),
                                       std::move(lock_path),
                                       std::move(pkg_identity),
                                       lock_acquired_at } };
}

cache::path cache::scoped_entry_lock::install_dir() const {
  return m->entry_dir_ / "install";
}

void cache::scoped_entry_lock::mark_install_complete() { m->completed_ = true; }
void cache::scoped_entry_lock::mark_user_managed() { m->user_managed_ = true; }

void cache::scoped_entry_lock::mark_fetch_complete() {
  std::filesystem::create_directories(fetch_dir());
  platform::touch_file(fetch_dir() / "envy-complete");
}

bool cache::scoped_entry_lock::is_install_complete() const { return m->completed_; }

bool cache::scoped_entry_lock::is_fetch_complete() const {
  return std::filesystem::exists(fetch_dir() / "envy-complete");
}

cache::path cache::scoped_entry_lock::stage_dir() const { return work_dir() / "stage"; }
cache::path cache::scoped_entry_lock::fetch_dir() const { return m->entry_dir_ / "fetch"; }
cache::path cache::scoped_entry_lock::work_dir() const { return m->entry_dir_ / "work"; }
cache::path cache::scoped_entry_lock::tmp_dir() const { return work_dir() / "tmp"; }

cache::cache(std::optional<path> root) : m{ std::make_unique<impl>() } {
  if (std::optional<path> maybe_root{ root ? root : platform::get_default_cache_root() }) {
    m->root_ = *maybe_root;
    return;
  }

  std::ostringstream oss;
  oss << "Unable to determine default cache root: "
      << platform::get_default_cache_root_env_vars() << " not set";
  throw std::runtime_error(oss.str());
}

cache::~cache() = default;

path const &cache::root() const { return m->root_; }

bool cache::is_entry_complete(path const &entry_dir) {
  path const complete_marker{ entry_dir / "envy-complete" };
  bool const exists{ platform::file_exists(complete_marker) };
  ENVY_TRACE_FILE_EXISTS_CHECK("", complete_marker.string(), exists);
  return exists;
}

cache::ensure_result cache::ensure_pkg(std::string_view identity,
                                       std::string_view platform,
                                       std::string_view arch,
                                       std::string_view hash_prefix) {
  std::string const variant{ [&] {
    std::ostringstream oss;
    oss << platform << "-" << arch << "-blake3-" << hash_prefix;
    return oss.str();
  }() };

  std::string const lock_name{ [&] {
    std::ostringstream oss;
    oss << "packages." << identity << "." << variant << ".lock";
    return oss.str();
  }() };

  path const entry_dir{ m->packages_dir() / std::string(identity) / variant };

  return ensure_entry(*m, entry_dir, m->locks_dir() / lock_name, identity, variant);
}

cache::ensure_result cache::ensure_spec(std::string_view identity) {
  std::string const id{ identity };
  return ensure_entry(*m,
                      m->specs_dir() / id,
                      m->locks_dir() / ("spec." + id + ".lock"),
                      identity,
                      id);
}

cache::path cache::ensure_envy(std::string_view version,
                               path const &exe_path,
                               std::string_view type_definitions) {
  path const envy_dir{ m->root_ / "envy" / std::string{ version } };
#ifdef _WIN32
  path const binary_path{ envy_dir / "envy.exe" };
#else
  path const binary_path{ envy_dir / "envy" };
#endif
  path const types_path{ envy_dir / "envy.lua" };

  if (std::filesystem::exists(binary_path) && std::filesystem::exists(types_path)) {
    shell_hooks::ensure(m->root_);
    return envy_dir;
  }

  std::filesystem::create_directories(m->locks_dir());
  platform::file_lock lock{ m->locks_dir() /
                            ("envy." + std::string{ version } + ".lock") };
  if (!lock) { return envy_dir; }

  // Re-check after lock (another process may have completed)
  if (std::filesystem::exists(binary_path) && std::filesystem::exists(types_path)) {
    shell_hooks::ensure(m->root_);
    return envy_dir;
  }

  std::error_code ec;
  std::filesystem::create_directories(envy_dir, ec);
  if (ec) { return envy_dir; }

  if (!copy_binary(exe_path, binary_path)) { return envy_dir; }
  util_write_file(types_path, type_definitions);
  shell_hooks::ensure(m->root_);

  return envy_dir;
}

}  // namespace envy

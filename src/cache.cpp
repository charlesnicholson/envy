#include "cache.h"

#include "platform.h"
#include "trace.h"
#include "tui.h"

#include <chrono>
#include <sstream>
#include <stdexcept>
#include <system_error>

using path = std::filesystem::path;

namespace envy {

struct cache_impl {
  path root_;

  path recipes_dir() const { return root_ / "recipes"; }
  path assets_dir() const { return root_ / "assets"; }
  path locks_dir() const { return root_ / "locks"; }
};

struct cache::impl : cache_impl {};

struct cache::scoped_entry_lock::impl {
  path entry_dir_;
  platform::file_lock lock_;
  path lock_path_;
  std::string recipe_identity_;
  std::chrono::steady_clock::time_point lock_acquired_time{};
  bool completed_{ false };
  bool user_managed_{ false };

  impl(path entry_dir,
       platform::file_lock lock,
       path lock_path,
       std::string recipe_identity,
       std::chrono::steady_clock::time_point lock_acquired_at)
      : entry_dir_{ std::move(entry_dir) },
        lock_{ std::move(lock) },
        lock_path_{ std::move(lock_path) },
        recipe_identity_{ std::move(recipe_identity) },
        lock_acquired_time{ lock_acquired_at } {}

  path asset_dir() const { return entry_dir_ / "asset"; }
};

}  // namespace envy

namespace {

void remove_all_noexcept(path const &target) {
  std::error_code ec;
  std::filesystem::remove_all(target, ec);
  if (ec) {
    envy::tui::error("Failed to remove %s: %s",
                     target.string().c_str(),
                     ec.message().c_str());
  }
}

envy::cache::ensure_result ensure_entry(envy::cache_impl &impl,
                                        path const &entry_dir,
                                        path const &lock_path,
                                        std::string_view recipe_identity,
                                        std::string_view cache_key) {
  envy::cache::ensure_result result{ entry_dir, entry_dir / "asset", nullptr };

  ENVY_TRACE_CACHE_CHECK_ENTRY(std::string(recipe_identity),
                                entry_dir.string(),
                                "before_lock");
  bool const complete_before_lock{ envy::cache::is_entry_complete(entry_dir) };
  ENVY_TRACE_CACHE_CHECK_RESULT(std::string(recipe_identity),
                                 entry_dir.string(),
                                 complete_before_lock,
                                 "before_lock");

  if (complete_before_lock) {
    ENVY_TRACE_CACHE_HIT(std::string(recipe_identity),
                         std::string(cache_key),
                         result.asset_path.string(),
                         true);
    return result;
  }

  std::filesystem::create_directories(impl.locks_dir());
  std::filesystem::create_directories(entry_dir);

  auto const lock_wait_start{ std::chrono::steady_clock::now() };
  envy::platform::file_lock lock{ lock_path };
  auto const wait_duration_ms{
    std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - lock_wait_start)
        .count()
  };
  ENVY_TRACE_LOCK_ACQUIRED(std::string(recipe_identity),
                           lock_path.string(),
                           static_cast<std::int64_t>(wait_duration_ms));

  ENVY_TRACE_CACHE_CHECK_ENTRY(std::string(recipe_identity),
                                entry_dir.string(),
                                "after_lock");
  bool const complete_after_lock{ envy::cache::is_entry_complete(entry_dir) };
  ENVY_TRACE_CACHE_CHECK_RESULT(std::string(recipe_identity),
                                 entry_dir.string(),
                                 complete_after_lock,
                                 "after_lock");

  if (complete_after_lock) {
    ENVY_TRACE_CACHE_HIT(std::string(recipe_identity),
                         std::string(cache_key),
                         result.asset_path.string(),
                         false);
    return result;
  }

  ENVY_TRACE_CACHE_MISS(std::string(recipe_identity), std::string(cache_key));
  auto const lock_acquired_at{ std::chrono::steady_clock::now() };
  result.lock = envy::cache::scoped_entry_lock::make(entry_dir,
                                                     std::move(lock),
                                                     lock_path,
                                                     std::string{ recipe_identity },
                                                     lock_acquired_at);
  return result;
}

}  // namespace

namespace envy {

cache::scoped_entry_lock::scoped_entry_lock(
    path entry_dir,
    platform::file_lock lock,
    path lock_path,
    std::string recipe_identity,
    std::chrono::steady_clock::time_point lock_acquired_at)
    : m{ std::make_unique<impl>(std::move(entry_dir),
                                std::move(lock),
                                std::move(lock_path),
                                std::move(recipe_identity),
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
  tui::debug("scoped_entry_lock CTOR: done");
}

cache::scoped_entry_lock::~scoped_entry_lock() {
  auto const hold_duration_ms{
    std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - m->lock_acquired_time)
        .count()
  };
  tui::debug("scoped_entry_lock DTOR: entry_dir=%s completed=%s user_managed=%s",
             m->entry_dir_.string().c_str(),
             m->completed_ ? "true" : "false",
             m->user_managed_ ? "true" : "false");

  if (m->completed_) {
    tui::debug("  DTOR: SUCCESS PATH - renaming install_dir -> asset_dir");
    remove_all_noexcept(m->asset_dir());
    tui::debug("    from: %s", install_dir().string().c_str());
    tui::debug("    to:   %s", m->asset_dir().string().c_str());
    platform::atomic_rename(install_dir(), m->asset_dir());
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
      remove_all_noexcept(m->asset_dir());
    }
  }

  tui::debug(
      "  DTOR: lock will be released and lock file deleted by file_lock destructor");
  ENVY_TRACE_LOCK_RELEASED(m->recipe_identity_,
                           m->lock_path_.string(),
                           static_cast<std::int64_t>(hold_duration_ms));
  tui::debug("scoped_entry_lock DTOR: done");
}

cache::scoped_entry_lock::ptr_t cache::scoped_entry_lock::make(
    path entry_dir,
    platform::file_lock lock_handle,
    path lock_path,
    std::string recipe_identity,
    std::chrono::steady_clock::time_point lock_acquired_at) {
  return ptr_t{ new scoped_entry_lock{ std::move(entry_dir),
                                       std::move(lock_handle),
                                       std::move(lock_path),
                                       std::move(recipe_identity),
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

cache::cache(std::optional<std::filesystem::path> root) : m{ std::make_unique<impl>() } {
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

cache::ensure_result cache::ensure_asset(std::string_view identity,
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
    oss << "assets." << identity << "." << variant << ".lock";
    return oss.str();
  }() };

  path const entry_dir{ m->assets_dir() / std::string(identity) / variant };

  return ensure_entry(*m, entry_dir, m->locks_dir() / lock_name, identity, variant);
}

cache::ensure_result cache::ensure_recipe(std::string_view identity) {
  std::string const id{ identity };
  return ensure_entry(*m,
                      m->recipes_dir() / id,
                      m->locks_dir() / ("recipe." + id + ".lock"),
                      identity,
                      id);
}

}  // namespace envy

#include "cache.h"

#include "platform.h"
#include "tui.h"

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
  path lock_path_;  // Needed for cleanup in destructor
  platform::file_lock lock_;
  bool completed_{ false };

  impl(path entry_dir, path lock_path, platform::file_lock lock)
      : entry_dir_{ std::move(entry_dir) },
        lock_path_{ std::move(lock_path) },
        lock_{ std::move(lock) } {}

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
                                        path const &lock_path) {
  envy::cache::ensure_result result{ entry_dir, entry_dir / "asset", nullptr };

  envy::tui::trace("ensure_entry: checking %s", entry_dir.string().c_str());

  if (envy::cache::is_entry_complete(entry_dir)) {
    envy::tui::trace("  ensure_entry: FAST PATH - entry already complete");
    return result;
  }

  envy::tui::trace("  ensure_entry: not complete, acquiring lock %s",
                   lock_path.string().c_str());
  std::filesystem::create_directories(impl.locks_dir());
  std::filesystem::create_directories(entry_dir);

  envy::tui::trace("  ensure_entry: blocking on file_lock acquisition...");
  envy::platform::file_lock lock{ lock_path };
  envy::tui::trace("  ensure_entry: acquired lock!");

  if (envy::cache::is_entry_complete(entry_dir)) {
    envy::tui::trace("  ensure_entry: SLOW PATH - completed while waiting for lock");
    // Lock automatically released when lock goes out of scope
    return result;
  }

  envy::tui::trace("  ensure_entry: CACHE MISS - returning lock for work");
  result.lock = envy::cache::scoped_entry_lock::make(entry_dir, lock_path, std::move(lock));
  return result;
}

}  // namespace

namespace envy {

cache::scoped_entry_lock::scoped_entry_lock(path entry_dir,
                                            path lock_path,
                                            platform::file_lock lock)
    : m{ std::make_unique<impl>(std::move(entry_dir), std::move(lock_path), std::move(lock)) } {

  tui::trace("scoped_entry_lock CTOR: entry_dir=%s", m->entry_dir_.string().c_str());
  tui::trace("  lock_path=%s", m->lock_path_.string().c_str());
  tui::trace("  about to remove_all(install_dir)");
  remove_all_noexcept(install_dir());
  tui::trace("  about to remove_all(work_dir)");
  remove_all_noexcept(work_dir());  // always delete (purely ephemeral)

  // Preserve fetch/ to enable per-file caching across failed attempts

  // Ensure directories exist
  tui::trace("  creating directories");
  std::filesystem::create_directories(fetch_dir());
  std::filesystem::create_directories(install_dir());
  std::filesystem::create_directories(stage_dir());
  tui::trace("scoped_entry_lock CTOR: done");
}

cache::scoped_entry_lock::~scoped_entry_lock() {
  tui::trace("scoped_entry_lock DTOR: entry_dir=%s completed=%s",
             m->entry_dir_.string().c_str(),
             m->completed_ ? "true" : "false");

  if (m->completed_) {
    tui::trace("  DTOR: removing asset_dir");
    remove_all_noexcept(m->asset_dir());
    tui::trace("  DTOR: renaming install_dir -> asset_dir");
    tui::trace("    from: %s", install_dir().string().c_str());
    tui::trace("    to:   %s", m->asset_dir().string().c_str());
    platform::atomic_rename(install_dir(), m->asset_dir());
    tui::trace("  DTOR: cleaning up work/fetch dirs");
    remove_all_noexcept(work_dir());
    remove_all_noexcept(fetch_dir());
    tui::trace("  DTOR: touching envy-complete");
    platform::touch_file(m->entry_dir_ / "envy-complete");
    tui::trace("  DTOR: completed path success");
  } else {
    tui::trace("  DTOR: not completed, cleaning up");
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

    tui::trace("  DTOR: install_empty=%s fetch_empty=%s",
               install_dir_empty ? "true" : "false",
               fetch_dir_empty ? "true" : "false");

    remove_all_noexcept(install_dir());
    remove_all_noexcept(work_dir());

    // If both install_dir and fetch_dir were completely empty, wipe entire cache entry
    if (install_dir_empty && fetch_dir_empty) {
      tui::trace("  DTOR: both empty, wiping entry");
      remove_all_noexcept(fetch_dir());
      remove_all_noexcept(m->asset_dir());
    }
  }

  tui::trace("  DTOR: unlocking file (automatic via file_lock destructor)");
  // Lock automatically released when m->lock_handle_ is destroyed
  std::filesystem::remove(m->lock_path_);
  tui::trace("scoped_entry_lock DTOR: done");
}

cache::scoped_entry_lock::ptr_t cache::scoped_entry_lock::make(
    path entry_dir,
    path lock_path,
    platform::file_lock lock_handle) {
  return ptr_t{ new scoped_entry_lock{ std::move(entry_dir),
                                       std::move(lock_path),
                                       std::move(lock_handle) } };
}

cache::path cache::scoped_entry_lock::install_dir() const {
  return m->entry_dir_ / "install";
}

void cache::scoped_entry_lock::mark_install_complete() { m->completed_ = true; }

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
  return std::filesystem::exists(entry_dir / "envy-complete");
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

  return ensure_entry(*m, entry_dir, m->locks_dir() / lock_name);
}

cache::ensure_result cache::ensure_recipe(std::string_view identity) {
  std::string const id{ identity };
  return ensure_entry(*m,
                      m->recipes_dir() / id,
                      m->locks_dir() / ("recipe." + id + ".lock"));
}

}  // namespace envy

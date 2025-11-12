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
  path lock_path_;
  platform::file_lock_handle_t lock_handle_{ platform::kInvalidLockHandle };
  bool completed_{ false };

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

  if (envy::cache::is_entry_complete(entry_dir)) { return result; }

  std::filesystem::create_directories(impl.locks_dir());
  std::filesystem::create_directories(entry_dir);

  envy::platform::file_lock_handle_t h{ envy::platform::lock_file(lock_path) };

  if (envy::cache::is_entry_complete(entry_dir)) {
    envy::platform::unlock_file(h);
    return result;
  }

  result.lock = envy::cache::scoped_entry_lock::make(entry_dir, lock_path, h);
  return result;
}

}  // namespace

namespace envy {

cache::scoped_entry_lock::scoped_entry_lock(path entry_dir,
                                            path lock_path,
                                            platform::file_lock_handle_t prelocked_handle)
    : m{ std::make_unique<impl>() } {
  m->entry_dir_ = std::move(entry_dir);
  m->lock_path_ = std::move(lock_path);
  m->lock_handle_ = prelocked_handle;

  remove_all_noexcept(install_dir());
  remove_all_noexcept(work_dir());  // always delete (purely ephemeral)

  // Preserve fetch/ to enable per-file caching across failed attempts

  // Ensure directories exist
  std::filesystem::create_directories(fetch_dir());
  std::filesystem::create_directories(install_dir());
  std::filesystem::create_directories(stage_dir());
}

cache::scoped_entry_lock::~scoped_entry_lock() {
  if (m->completed_) {
    remove_all_noexcept(m->asset_dir());
    platform::atomic_rename(install_dir(), m->asset_dir());
    remove_all_noexcept(work_dir());
    remove_all_noexcept(fetch_dir());
    platform::touch_file(m->entry_dir_ / "envy-complete");
  } else {
    // Check if install_dir AND fetch_dir are empty (programmatic package didn't use cache
    // at all)
    std::error_code ec;
    bool install_dir_empty{ true };
    for (std::filesystem::directory_iterator it{ install_dir(), ec };
         !ec && it != std::filesystem::directory_iterator{};
         ++it) {
      install_dir_empty = false;
      break;
    }

    bool fetch_dir_empty{ true };
    for (std::filesystem::directory_iterator it{ fetch_dir(), ec };
         !ec && it != std::filesystem::directory_iterator{};
         ++it) {
      fetch_dir_empty = false;
      break;
    }

    remove_all_noexcept(install_dir());
    remove_all_noexcept(work_dir());

    // If both install_dir and fetch_dir were completely empty, wipe entire cache entry
    if (!ec && install_dir_empty && fetch_dir_empty) {
      remove_all_noexcept(fetch_dir());
      remove_all_noexcept(m->asset_dir());
    }
  }

  platform::unlock_file(m->lock_handle_);
  std::filesystem::remove(m->lock_path_);
}

cache::scoped_entry_lock::ptr_t cache::scoped_entry_lock::make(
    path entry_dir,
    path lock_path,
    platform::file_lock_handle_t lock_handle) {
  return ptr_t{
    new scoped_entry_lock{ std::move(entry_dir), std::move(lock_path), lock_handle }
  };
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
    oss << platform << "-" << arch << "-sha256-" << hash_prefix;
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

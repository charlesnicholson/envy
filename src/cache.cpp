#include "cache.h"

#include "platform.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <system_error>

using path = std::filesystem::path;

namespace envy {

namespace {

void remove_all_noexcept(path const &target) {
  std::error_code ec;
  std::filesystem::remove_all(target, ec);
}

}  // namespace

cache::scoped_entry_lock::scoped_entry_lock(path entry_dir,
                                            path install_dir,
                                            path stage_dir,
                                            path fetch_dir,
                                            path lock_path)
    : entry_dir_{ std::move(entry_dir) },
      install_dir_{ std::move(install_dir) },
      stage_dir_{ std::move(stage_dir) },
      fetch_dir_{ std::move(fetch_dir) },
      lock_path_{ std::move(lock_path) },
      lock_handle_{ platform::lock_file(lock_path_) } {}

cache::scoped_entry_lock::~scoped_entry_lock() {
  if (completed_) {
    remove_all_noexcept(stage_dir_);
    remove_all_noexcept(fetch_dir_);
    remove_all_noexcept(asset_dir());
    platform::atomic_rename(install_dir_, asset_dir());
    remove_all_noexcept(fetch_dir_.parent_path());
    std::ofstream{ entry_dir_ / ".envy-complete" }.close();
  } else {
    remove_all_noexcept(install_dir_);
    remove_all_noexcept(stage_dir_);
    remove_all_noexcept(fetch_dir_);
    remove_all_noexcept(fetch_dir_.parent_path());
  }

  if (lock_handle_ != platform::kInvalidLockHandle) {
    platform::unlock_file(lock_handle_);
    lock_handle_ = platform::kInvalidLockHandle;
  }
  std::filesystem::remove(lock_path_);
}

void cache::scoped_entry_lock::mark_complete() { completed_ = true; }

cache::scoped_entry_lock::ptr_t cache::scoped_entry_lock::make(path entry_dir,
                                                               path install_dir,
                                                               path stage_dir,
                                                               path fetch_dir,
                                                               path lock_path) {
  return ptr_t{ new scoped_entry_lock{ std::move(entry_dir),
                                       std::move(install_dir),
                                       std::move(stage_dir),
                                       std::move(fetch_dir),
                                       std::move(lock_path) } };
}

cache::cache(std::optional<std::filesystem::path> root) {
  if (std::optional<path> maybe_root{ root ? root : platform::get_default_cache_root() }) {
    root_ = *maybe_root;
    return;
  }

  std::ostringstream oss;
  oss << "Unable to determine default cache root: "
      << platform::get_default_cache_root_env_vars() << " not set";
  throw std::runtime_error(oss.str());
}

path const &cache::root() const { return root_; }

bool cache::is_entry_complete(path const &entry_dir) {
  return std::filesystem::exists(entry_dir / ".envy-complete");
}

path cache::recipes_dir() const { return root_ / "recipes"; }
path cache::assets_dir() const { return root_ / "assets"; }
path cache::locks_dir() const { return root_ / "locks"; }

cache::ensure_result cache::ensure_entry(path const &entry_dir, path const &lock_path) {
  ensure_result result{};
  result.entry_path = entry_dir;
  result.asset_path = entry_dir / "asset";

  if (is_entry_complete(entry_dir)) { return result; }

  std::filesystem::create_directories(locks_dir());
  std::filesystem::create_directories(entry_dir);

  path const install_dir{ entry_dir / ".install" };
  path const work_dir{ entry_dir / ".work" };
  path const fetch_dir{ work_dir / "fetch" };
  path const stage_dir{ work_dir / "stage" };

  auto lock{
    scoped_entry_lock::make(entry_dir, install_dir, stage_dir, fetch_dir, lock_path)
  };

  // Re-check (other envy may have finished while we waited)
  if (is_entry_complete(entry_dir)) {
    lock.reset();
    return result;
  }

  remove_all_noexcept(install_dir);
  remove_all_noexcept(work_dir);
  std::filesystem::create_directories(install_dir);
  std::filesystem::create_directories(fetch_dir);
  std::filesystem::create_directories(stage_dir);

  result.lock = std::move(lock);
  return result;
}

cache::ensure_result cache::ensure_asset(std::string_view identity,
                                         std::string_view platform,
                                         std::string_view arch,
                                         std::string_view hash_prefix) {
  std::string const entry{ [&] {
    std::ostringstream oss;
    oss << identity << "." << platform << "-" << arch << "-sha256-" << hash_prefix;
    return oss.str();
  }() };

  std::string const lock{ [&] {
    std::ostringstream oss;
    oss << "assets." << entry << ".lock";
    return oss.str();
  }() };

  return ensure_entry(assets_dir() / entry, locks_dir() / lock);
}

cache::ensure_result cache::ensure_recipe(std::string_view identity) {
  path const entry_dir{ recipes_dir() / (std::string{ identity } + ".lua") };
  std::ostringstream oss;
  oss << "recipe." << identity << ".lock";
  return ensure_entry(entry_dir, { locks_dir() / oss.str() });
}

}  // namespace envy

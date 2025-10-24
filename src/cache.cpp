#include "cache.h"

#include "platform.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

using path = std::filesystem::path;

namespace envy {

cache::scoped_entry_lock::scoped_entry_lock(path entry_dir,
                                            path stage_dir,
                                            path lock_path,
                                            file_lock::ptr_t lock)
    : entry_dir_{ std::move(entry_dir) },
      stage_dir_{ std::move(stage_dir) },
      lock_path_{ std::move(lock_path) },
      lock_{ std::move(lock) } {}

cache::scoped_entry_lock::~scoped_entry_lock() {
  if (completed_) {
    std::ofstream{ stage_dir_ / ".envy-complete" }.close();
    platform::atomic_rename(stage_dir_, entry_dir_);
  }

  lock_.reset();  // Close lock handle before deleting lock file (Windows compatibility)
  std::filesystem::remove(lock_path_);
}

void cache::scoped_entry_lock::mark_complete() { completed_ = true; }

cache::scoped_entry_lock::ptr_t cache::scoped_entry_lock::make(path entry_dir,
                                                               path staging_dir,
                                                               path lock_path,
                                                               file_lock::ptr_t lock) {
  return ptr_t{ new scoped_entry_lock{ std::move(entry_dir),
                                       std::move(staging_dir),
                                       std::move(lock_path),
                                       std::move(lock) } };
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
path cache::assets_dir() const { return root_ / "deployed"; }
path cache::locks_dir() const { return root_ / "locks"; }

cache::ensure_result cache::ensure_entry(path const &entry_dir, path const &lock_path) {
  if (is_entry_complete(entry_dir)) { return { entry_dir, nullptr }; }

  std::filesystem::create_directories(locks_dir());
  auto lock{ file_lock::make(lock_path) };

  // re-check (other envy may have finished while we waited)
  if (is_entry_complete(entry_dir)) {
    lock.reset();  // Close lock before deleting lock file (Windows compatibility)
    std::filesystem::remove(lock_path);
    return { entry_dir, nullptr };
  }

  path const staging{ entry_dir.string() + ".inprogress" };
  if (std::filesystem::exists(staging)) { std::filesystem::remove_all(staging); }
  std::filesystem::create_directories(staging);

  return { staging,
           scoped_entry_lock::make(entry_dir, staging, lock_path, std::move(lock)) };
}

cache::ensure_result cache::ensure_asset(std::string_view identity,
                                         std::string_view platform,
                                         std::string_view arch,
                                         std::string_view hash_prefix) {
  std::ostringstream oss;
  oss << identity << "." << platform << "-" << arch << "-sha256-" << hash_prefix;
  path const entry_dir{ assets_dir() / oss.str() };

  oss.str("");
  oss << "deployed." << identity << "." << platform << "-" << arch << "-sha256-"
      << hash_prefix << ".lock";
  path const lock_path{ locks_dir() / oss.str() };

  return ensure_entry(entry_dir, lock_path);
}

cache::ensure_result cache::ensure_recipe(std::string_view identity) {
  path const entry_dir{ recipes_dir() / (std::string{ identity } + ".lua") };
  std::ostringstream oss;
  oss << "recipe." << identity << ".lock";
  return ensure_entry(entry_dir, { locks_dir() / oss.str() });
}

}  // namespace envy

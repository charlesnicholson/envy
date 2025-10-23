#include "cache.h"

#include "platform.h"

#include <sstream>
#include <stdexcept>

namespace envy {

cache::cache(std::optional<std::filesystem::path> root) {
  if (std::optional<std::filesystem::path> maybe_root{
          root ? root : platform::get_default_cache_root() }) {
    root_ = *maybe_root;
    return;
  }

  std::ostringstream oss;
  oss << "Unable to determine default cache root: "
      << platform::get_default_cache_root_env_vars() << " not set";
  throw std::runtime_error(oss.str());
}

std::filesystem::path const &cache::root() const { return root_; }

bool cache::is_entry_complete(std::filesystem::path const &entry_dir) {
  return std::filesystem::exists(entry_dir / ".envy-complete");
}

cache::scoped_entry_lock::scoped_entry_lock(std::filesystem::path entry_dir,
                                            std::filesystem::path lock_path,
                                            file_lock lock)
    : entry_dir_{ std::move(entry_dir) },
      lock_path_{ std::move(lock_path) },
      lock_{ std::move(lock) } {}

void cache::scoped_entry_lock::mark_complete() { completed_ = true; }

std::filesystem::path cache::scoped_entry_lock::create_staging() {
  std::filesystem::path const staging_path{ entry_dir_.string() + ".inprogress" };
  std::filesystem::create_directories(staging_path);
  return staging_path;
}

std::filesystem::path cache::locks_dir() const { return root_ / "locks"; }

std::filesystem::path cache::get_lock_path(std::string_view identity,
                                           std::string_view platform,
                                           std::string_view arch,
                                           std::string_view hash_prefix) const {
  std::ostringstream oss;
  oss << "deployed." << identity << "." << platform << "-" << arch << "-sha256-"
      << hash_prefix << ".lock";
  return locks_dir() / oss.str();
}

}  // namespace envy

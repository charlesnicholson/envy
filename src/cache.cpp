#include "cache.h"

#include "platform.h"
#include "tui.h"

#include <sstream>
#include <stdexcept>
#include <system_error>

using path = std::filesystem::path;

namespace envy {

namespace {

void remove_all_noexcept(path const &target) {
  std::error_code ec;
  std::filesystem::remove_all(target, ec);
  if (ec) {
    tui::error("Failed to remove %s: %s", target.string().c_str(), ec.message().c_str());
  }
}

}  // namespace

cache::scoped_entry_lock::scoped_entry_lock(path entry_dir,
                                           path lock_path,
                                           platform::file_lock_handle_t prelocked_handle)
    : entry_dir_{ std::move(entry_dir) },
      lock_path_{ std::move(lock_path) },
      lock_handle_{ prelocked_handle } {
  remove_all_noexcept(install_dir());

  // Conditionally preserve fetch/ if marked complete
  if (!is_fetch_complete()) {
    remove_all_noexcept(work_dir());
    std::filesystem::create_directories(fetch_dir());
  }

  // Always recreate staging (ephemeral)
  remove_all_noexcept(stage_dir());
  std::filesystem::create_directories(install_dir());
  std::filesystem::create_directories(stage_dir());
}

cache::scoped_entry_lock::~scoped_entry_lock() {
  if (completed_) {
    remove_all_noexcept(asset_dir());
    platform::atomic_rename(install_dir(), asset_dir());
    remove_all_noexcept(work_dir());
    platform::touch_file(entry_dir_ / ".envy-complete");
  } else {
    remove_all_noexcept(install_dir());
    remove_all_noexcept(stage_dir());
  }

  platform::unlock_file(lock_handle_);
  std::filesystem::remove(lock_path_);
}

cache::scoped_entry_lock::ptr_t cache::scoped_entry_lock::make(path entry_dir,
                                                               path lock_path,
                                                               platform::file_lock_handle_t lock_handle) {
  return ptr_t{ new scoped_entry_lock{ std::move(entry_dir),
                                       std::move(lock_path),
                                       lock_handle } };
}

cache::path cache::scoped_entry_lock::install_dir() const {
  return entry_dir_ / ".install";
}

void cache::scoped_entry_lock::mark_complete() { completed_ = true; }

void cache::scoped_entry_lock::mark_fetch_complete() {
  std::filesystem::create_directories(fetch_dir());
  platform::touch_file(fetch_dir() / ".envy-complete");
}

bool cache::scoped_entry_lock::is_fetch_complete() const {
  return std::filesystem::exists(fetch_dir() / ".envy-complete");
}

cache::path cache::scoped_entry_lock::stage_dir() const { return work_dir() / "stage"; }
cache::path cache::scoped_entry_lock::fetch_dir() const { return work_dir() / "fetch"; }
cache::path cache::scoped_entry_lock::asset_dir() const { return entry_dir_ / "asset"; }
cache::path cache::scoped_entry_lock::work_dir() const { return entry_dir_ / ".work"; }

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

  platform::file_lock_handle_t h { platform::lock_file(lock_path)};

  if (is_entry_complete(entry_dir)) { // we lost a race but the work completed.
    platform::unlock_file(h);  
    return result;
  }

  result.lock = scoped_entry_lock::make(entry_dir, lock_path, h);
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

  std::string const lock{ "assets." + entry + ".lock" };

  return ensure_entry(assets_dir() / entry, locks_dir() / lock);
}

cache::ensure_result cache::ensure_recipe(std::string_view identity) {
  std::string const id{ identity };
  return ensure_entry(recipes_dir() / (id + ".lua"),
                      locks_dir() / ("recipe." + id + ".lock"));
}

}  // namespace envy

#pragma once

#include "platform.h"
#include "util.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>

namespace envy {

class cache : unmovable {
 public:
  using path = std::filesystem::path;

  class scoped_entry_lock : unmovable {
   public:
    using ptr_t = std::unique_ptr<scoped_entry_lock>;

    static ptr_t make(path entry_dir,
                      path install_dir,
                      path stage_dir,
                      path fetch_dir,
                      path lock_path);
    ~scoped_entry_lock();

    void mark_complete();

    path const &install_dir() const { return install_dir_; }
    path const &stage_dir() const { return stage_dir_; }
    path const &fetch_dir() const { return fetch_dir_; }

   private:
    scoped_entry_lock(path entry_dir,
                      path install_dir,
                      path stage_dir,
                      path fetch_dir,
                      path lock_path);

    path entry_dir_;
    path install_dir_;
    path stage_dir_;
    path fetch_dir_;
    path lock_path_;
    platform::file_lock_handle_t lock_handle_{ platform::kInvalidLockHandle };
    bool completed_{ false };

    path asset_dir() const { return entry_dir_ / "asset"; }
  };

  explicit cache(std::optional<path> root = std::nullopt);

  path const &root() const;

  struct ensure_result {
    path entry_path;                // entry directory containing metadata and asset/
    path asset_path;                // entry_path / "asset"
    scoped_entry_lock::ptr_t lock;  // if present, lock held for installation
  };

  ensure_result ensure_asset(std::string_view identity,
                             std::string_view platform,
                             std::string_view arch,
                             std::string_view hash_prefix);

  ensure_result ensure_recipe(std::string_view identity);

  static bool is_entry_complete(std::filesystem::path const &entry_dir);

 private:
  path root_;

  path recipes_dir() const;
  path assets_dir() const;
  path locks_dir() const;

  ensure_result ensure_entry(path const &entry_dir, path const &lock_path);
};

}  // namespace envy

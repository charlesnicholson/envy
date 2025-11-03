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
                      path lock_path,
                      platform::file_lock_handle_t lock_handle);
    ~scoped_entry_lock();

    void mark_install_complete();
    void mark_fetch_complete();
    bool is_fetch_complete() const;

    path install_dir() const;
    path stage_dir() const;
    path fetch_dir() const;
    path work_dir() const;

   private:
    scoped_entry_lock(path entry_dir,
                      path lock_path,
                      platform::file_lock_handle_t lock_handle);

    struct impl;
    std::unique_ptr<impl> m;
  };

  explicit cache(std::optional<path> root = std::nullopt);
  ~cache();

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
  struct impl;
  std::unique_ptr<impl> m;
};

}  // namespace envy

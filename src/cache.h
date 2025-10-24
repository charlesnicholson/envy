#pragma once

#include "file_lock.h"
#include "util.h"

#include <filesystem>
#include <memory>
#include <string_view>

namespace envy {

class cache : unmovable {
 public:
  class scoped_entry_lock : unmovable {
   public:
    using ptr_t = std::unique_ptr<scoped_entry_lock>;
    using path = std::filesystem::path;

    static ptr_t make(path entry_dir, path stage_dir, path lock_path, file_lock::ptr_t l);
    ~scoped_entry_lock();

    void mark_complete();

   private:
    scoped_entry_lock(path entry_dir, path stage_dir, path lock_path, file_lock::ptr_t l);

    path entry_dir_;
    path stage_dir_;
    path lock_path_;
    file_lock::ptr_t lock_;
    bool completed_{ false };
  };

  explicit cache(std::optional<std::filesystem::path> root = std::nullopt);

  std::filesystem::path const &root() const;

  struct ensure_result {
    std::filesystem::path path;     // stage path if locked, final path if complete
    scoped_entry_lock::ptr_t lock;  // if present, locked for installation
  };

  ensure_result ensure_asset(std::string_view identity,
                             std::string_view platform,
                             std::string_view arch,
                             std::string_view hash_prefix);

  ensure_result ensure_recipe(std::string_view identity);

  static bool is_entry_complete(std::filesystem::path const &entry_dir);

 private:
  std::filesystem::path root_;

  std::filesystem::path recipes_dir() const;
  std::filesystem::path assets_dir() const;
  std::filesystem::path locks_dir() const;

  ensure_result ensure_entry(std::filesystem::path const &entry_dir,
                             std::filesystem::path const &lock_path);
};

}  // namespace envy

#pragma once

#include "file_lock.h"
#include "util.h"

#include <filesystem>
#include <optional>
#include <string_view>

namespace envy {

class cache {
 public:
  class scoped_lock : unmovable {
   public:
    ~scoped_lock();

    // optional working dir for assets, atomically renamed to cache dir in commit_staging.
    // lives in a special location in the cache, same volume, etc.
    std::optional<std::filesystem::path> create_staging();
    void commit_staging(std::filesystem::path const &staging_dir);

   private:
    friend class cache;

    std::filesystem::path entry_dir_;
    file_lock lock_;

    scoped_lock(std::filesystem::path entry_dir, file_lock lock);
  };

  struct ensure_result {
    std::filesystem::path path;       // asset path
    std::optional<scoped_lock> lock;  // if valid, locked for installation.
  };

  explicit cache(std::optional<std::filesystem::path> root = std::nullopt);

  std::filesystem::path const &root() const { return root_; }

  ensure_result ensure_asset(std::string_view identity,
                             std::string_view platform,
                             std::string_view arch,
                             std::string_view hash_prefix);

  ensure_result ensure_recipe(std::string_view identity);

 private:
  std::filesystem::path root_;

  std::filesystem::path recipes_dir() const;
  std::filesystem::path assets_dir() const;
  std::filesystem::path locks_dir() const;

  static bool is_entry_complete(std::filesystem::path const &entry_dir);

  std::filesystem::path get_lock_path(std::string_view identity,
                                      std::string_view platform,
                                      std::string_view arch,
                                      std::string_view hash_prefix) const;
};

}  // namespace envy

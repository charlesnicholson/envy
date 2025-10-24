#pragma once

#include "file_lock.h"
#include "util.h"

#include <filesystem>
#include <optional>
#include <string_view>

namespace envy {

class cache : unmovable {
 public:
  class scoped_entry_lock : uncopyable {
   public:
    ~scoped_entry_lock();
    scoped_entry_lock(scoped_entry_lock &&other) noexcept;
    scoped_entry_lock &operator=(scoped_entry_lock &&other) noexcept;

    void mark_complete();

   private:
    friend class cache;

    std::filesystem::path entry_dir_;
    std::filesystem::path staging_dir_;
    std::filesystem::path lock_path_;
    file_lock lock_;
    bool completed_{ false };
    bool moved_from_{ false };

    scoped_entry_lock(std::filesystem::path entry_dir,
                      std::filesystem::path staging_dir,
                      std::filesystem::path lock_path,
                      file_lock lock);
  };

  explicit cache(std::optional<std::filesystem::path> root = std::nullopt);

  std::filesystem::path const &root() const;

  struct ensure_result {
    std::filesystem::path path;  // staging path if locked, final path if complete
    std::optional<scoped_entry_lock> lock;  // if present, locked for installation
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

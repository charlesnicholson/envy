#pragma once

#include "file_lock.h"
#include "util.h"

#include <filesystem>
#include <optional>
#include <string_view>
#include <system_error>

namespace envy {

class cache {
 public:
  explicit cache(std::optional<std::filesystem::path> root = std::nullopt);

  std::filesystem::path const &root() const { return root_; }

  enum class lookup_result { hit, miss, incomplete };

  class asset_lock : unmovable {
   public:
    ~asset_lock();

    std::filesystem::path const &asset_dir() const { return asset_dir_; }
    std::optional<std::filesystem::path> create_staging();
    std::error_code commit(std::filesystem::path const &staging_dir);

   private:
    friend class cache;

    std::filesystem::path asset_dir_;
    std::optional<file_lock> lock_;

    asset_lock(std::filesystem::path asset_dir, std::optional<file_lock> lock);
  };

  std::filesystem::path asset_path(std::string_view identity,
                                   std::string_view platform,
                                   std::string_view arch,
                                   std::string_view hash_prefix) const;

  lookup_result lookup_asset(std::string_view identity,
                             std::string_view platform,
                             std::string_view arch,
                             std::string_view hash_prefix) const;

  std::optional<asset_lock> lock_asset(std::string_view identity,
                                       std::string_view platform,
                                       std::string_view arch,
                                       std::string_view hash_prefix);

  std::size_t cleanup_stale_staging();

 private:
  std::filesystem::path root_;

  std::filesystem::path recipes_dir() const;
  std::filesystem::path assets_dir() const;
  std::filesystem::path locks_dir() const;

  static bool is_asset_complete(std::filesystem::path const &asset_dir);

  std::filesystem::path get_asset_lock_path(std::string_view identity,
                                            std::string_view platform,
                                            std::string_view arch,
                                            std::string_view hash_prefix) const;
};

}  // namespace envy

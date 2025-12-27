#pragma once

#include "platform.h"
#include "util.h"

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace envy {

class cache : unmovable {
 public:
  using path = std::filesystem::path;

  class scoped_entry_lock : unmovable {
   public:
    using ptr_t = std::unique_ptr<scoped_entry_lock>;

    static ptr_t make(path entry_dir,
                      platform::file_lock lock,
                      path lock_path,
                      std::string recipe_identity,
                      std::chrono::steady_clock::time_point lock_acquired_at);
    ~scoped_entry_lock();

    void mark_install_complete();
    void mark_user_managed();
    void mark_fetch_complete();
    bool is_install_complete() const;
    bool is_fetch_complete() const;

    path install_dir() const;
    path stage_dir() const;
    path fetch_dir() const;
    path work_dir() const;
    path tmp_dir() const;  // Ephemeral workspace for user-managed packages

   private:
    scoped_entry_lock(path entry_dir,
                      platform::file_lock lock,
                      path lock_path,
                      std::string recipe_identity,
                      std::chrono::steady_clock::time_point lock_acquired_at);

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

  // Ensure envy binary cache entry exists. Returns lock if installation needed.
  // Unlike ensure_asset/recipe, envy entries use flat structure (no asset/ subdir).
  struct ensure_envy_result {
    path envy_dir;   // $CACHE/envy/$VERSION
    bool needs_install;
  };
  ensure_envy_result ensure_envy(std::string_view version);

  static bool is_entry_complete(std::filesystem::path const &entry_dir);

 private:
  struct impl;
  std::unique_ptr<impl> m;
};

}  // namespace envy

#pragma once

#include "lua_util.h"
#include "phase.h"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <variant>

namespace envy {

struct recipe_spec {
  struct remote_source {
    std::string url;
    std::string sha256;
    std::optional<std::string> subdir;  // Path within archive to recipe entry point
  };

  struct local_source {
    std::filesystem::path file_path;  // Can be file or directory
  };

  struct git_source {
    std::string url;
    std::string ref;  // commit SHA or committish
    std::optional<std::string> subdir;
  };

  struct fetch_function {};  // Recipe defines custom fetch()

  using source_t = std::variant<remote_source, local_source, git_source, fetch_function>;

  std::string identity;  // "namespace.name@version"
  source_t source;
  std::unordered_map<std::string, lua_value> options;
  std::optional<phase> needed_by;  // Phase dependency annotation

  static recipe_spec parse(lua_value const &lua_val,
                           std::filesystem::path const &base_path);

  // Serialize lua_value to canonical string for stable recipe option hashing
  static std::string serialize_option_table(lua_value const &val);

  // Format canonical key: "identity" or "identity{opt=val,...}"
  // Used for logging, result maps, and any place needing a unique recipe identifier
  static std::string format_key(std::string const &identity,
                                std::unordered_map<std::string, lua_value> const &options);

  // Instance method convenience wrapper
  std::string format_key() const;

  bool is_remote() const;
  bool is_local() const;
  bool is_git() const;
  bool has_fetch_function() const;
};

}  // namespace envy

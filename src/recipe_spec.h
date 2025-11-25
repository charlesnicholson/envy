#pragma once

#include "lua_util.h"
#include "recipe_phase.h"

extern "C" {
#include "lua.h"
}

#include <filesystem>
#include <string>
#include <unordered_map>
#include <variant>

namespace envy {

struct recipe_spec : uncopyable {
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
  std::optional<std::string> alias;       // User-friendly short name
  std::optional<recipe_phase> needed_by;  // Phase dependency annotation

  // Custom source fetch (nested source dependencies)
  std::vector<recipe_spec> source_dependencies{};  // Needed for fetching this recipe

  recipe_spec() = default;
  ~recipe_spec() = default;
  recipe_spec(recipe_spec &&other) noexcept = default;
  recipe_spec &operator=(recipe_spec &&other) noexcept = default;

  // Parse recipe_spec from lua_value
  // lua_State required for custom source.fetch validation; pass nullptr for simple recipes
  static recipe_spec parse(lua_value const &lua_val,
                           std::filesystem::path const &base_path,
                           lua_State *L);

  // Parse recipe_spec directly from Lua stack (for tables containing functions)
  // Used primarily for testing; production code should use parse() with lua_value
  static recipe_spec parse_from_stack(lua_State *L,
                                      int index,
                                      std::filesystem::path const &base_path);

  // Serialize lua_value to canonical string for stable recipe option hashing
  static std::string serialize_option_table(lua_value const &val);

  // Format canonical key: "identity" or "identity{opt=val,...}"
  // Used for logging, result maps, and any place needing a unique recipe identifier
  static std::string format_key(std::string const &identity,
                                std::unordered_map<std::string, lua_value> const &options);

  std::string format_key() const;
  bool is_remote() const;
  bool is_local() const;
  bool is_git() const;
  bool has_fetch_function() const;

  // Look up source.fetch function for a dependency from Lua state's dependencies global
  // Pushes the function onto the stack if found, returns true
  // Returns false if not found (leaves stack unchanged)
  static bool lookup_and_push_source_fetch(lua_State *L, std::string const &dep_identity);
};

}  // namespace envy

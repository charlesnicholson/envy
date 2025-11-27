#pragma once

#include "recipe_phase.h"
#include "util.h"

#include "sol/sol.hpp"

#include <filesystem>
#include <string>
#include <variant>

namespace envy {

struct recipe_spec : uncopyable {
  recipe_spec() = default;
  ~recipe_spec() = default;
  recipe_spec(recipe_spec &&other) noexcept = default;
  recipe_spec &operator=(recipe_spec &&other) noexcept = default;

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
  std::string serialized_options;    // Serialized Lua table literal (empty "{}" if none)
  std::optional<recipe_phase> needed_by;  // Phase dependency annotation

  // Custom source fetch (nested source dependencies)
  std::vector<recipe_spec> source_dependencies{};  // Needed for fetching this recipe

  // Parse recipe_spec from Sol2 object
  static recipe_spec parse(sol::object const &lua_val,
                           std::filesystem::path const &base_path,
                           lua_State *L);

  // Parse recipe_spec directly from Lua stack (for tables containing functions)
  // Used primarily for testing; production code should use parse() with sol::object
  static recipe_spec parse_from_stack(lua_State *L,
                                      int index,
                                      std::filesystem::path const &base_path);

  // Serialize sol::object to canonical string for stable recipe option hashing
  static std::string serialize_option_table(sol::object const &val);

  // Format canonical key: "identity" or "identity{opt=val,...}"
  // Used for logging, result maps, and any place needing a unique recipe identifier
  static std::string format_key(std::string const &identity,
                                std::string const &serialized_options);

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

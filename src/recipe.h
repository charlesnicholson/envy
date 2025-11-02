#pragma once

#include "lua_util.h"
#include "phase.h"
#include "util.h"

#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace envy {

class recipe : unmovable {
 public:
  struct cfg {
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

    struct fetch_function {
      std::string lua_code;  // Serialized Lua function
    };

    using source_t = std::variant<remote_source, local_source, git_source, fetch_function>;

    std::string identity;  // "namespace.name@version"
    source_t source;
    std::unordered_map<std::string, std::string> options;
    std::optional<phase> needed_by;  // Phase dependency annotation

    static cfg parse(lua_value const &lua_val, std::filesystem::path const &base_path);

    bool is_remote() const;
    bool is_local() const;
    bool is_git() const;
    bool has_fetch_function() const;
  };

  recipe(cfg cfg, lua_state_ptr lua_state, std::vector<recipe *> dependencies);
  ~recipe();

  cfg const &config() const;
  std::string const &identity() const;
  std::string_view namespace_name() const;
  std::string_view name() const;
  std::string_view version() const;

  cfg::source_t const &source() const;
  lua_State *lua_state() const;

  std::vector<recipe *> const &dependencies() const;

 private:
  struct impl;
  std::unique_ptr<impl> m;
};

struct resolution_result {
  std::set<std::unique_ptr<recipe>> recipes;
  std::vector<recipe *> roots;
};

class cache;
resolution_result recipe_resolve(std::vector<recipe::cfg> const &packages, cache &c);

}  // namespace envy

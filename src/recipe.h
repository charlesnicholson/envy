#pragma once

#include "lua_util.h"
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
    };

    struct local_source {
      std::filesystem::path file_path;
    };

    using source_t = std::variant<remote_source, local_source>;

    std::string identity;  // "namespace.name@version"
    source_t source;
    std::unordered_map<std::string, std::string> options;

    static cfg parse(lua_value const &lua_val, std::filesystem::path const &base_path);

    bool is_remote() const;
    bool is_local() const;
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

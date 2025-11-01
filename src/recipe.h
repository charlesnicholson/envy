#pragma once

#include "cache.h"
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
  };

  recipe(cfg cfg, lua_state_ptr lua_state, std::vector<recipe *> dependencies);

  cfg const &config() const { return cfg_; }
  std::string const &identity() const { return cfg_.identity; }
  std::string_view namespace_name() const;
  std::string_view name() const;
  std::string_view version() const;

  cfg::source_t const &source() const { return cfg_.source; }
  lua_State *lua_state() const { return lua_state_.get(); }

  std::vector<recipe *> const &dependencies() const { return dependencies_; }

 private:
  cfg cfg_;
  lua_state_ptr lua_state_;
  std::vector<recipe *> dependencies_;
};

struct resolution_result {
  std::set<std::unique_ptr<recipe>> recipes;
  std::vector<recipe *> roots;
};

resolution_result recipe_resolve(std::vector<recipe::cfg> const &packages, cache &c);

}  // namespace envy

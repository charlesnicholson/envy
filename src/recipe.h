#pragma once

#include "lua_util.h"
#include "phase.h"
#include "util.h"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>

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

    struct fetch_function {};  // Recipe defines custom fetch()

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

  recipe(cfg cfg, lua_state_ptr lua_state);
  ~recipe();

  cfg const &config() const;
  std::string const &identity() const;
  std::string_view namespace_name() const;
  std::string_view name() const;
  std::string_view version() const;

  cfg::source_t const &source() const;
  lua_State *lua_state() const;

 private:
  struct impl;
  std::unique_ptr<impl> m;
};

}  // namespace envy

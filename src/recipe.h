#pragma once

#include "util.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace envy {

class lua_value;

class recipe : unmovable {
 public:
  // Recipe configuration (identity + source + options)
  struct cfg {
    struct builtin_source {};

    struct remote_source {
      std::string url;
      std::string sha256;
    };

    struct local_source {
      std::filesystem::path file_path;
    };

    using source_t = std::variant<builtin_source, remote_source, local_source>;

    std::string identity;  // "namespace.name@version"
    source_t source;
    std::unordered_map<std::string, std::string> options;

    static cfg parse(lua_value const &lua_val, std::filesystem::path const &base_path);
  };

  explicit recipe(cfg cfg);

  cfg const &config() const { return cfg_; }
  std::string const &identity() const { return cfg_.identity; }
  std::string_view namespace_name() const;
  std::string_view name() const;
  std::string_view version() const;

  cfg::source_t const &source() const { return cfg_.source; }

  std::vector<recipe const *> const &dependencies() const { return dependencies_; }
  void add_dependency(recipe const *dep);

 private:
  cfg cfg_;
  std::vector<recipe const *> dependencies_;
};

}  // namespace envy

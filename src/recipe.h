#pragma once

#include "util.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace envy {

class cache;

class recipe : unmovable {
 public:
  struct builtin_source {};

  struct remote_source {
    std::string url;
    std::string sha256;
    std::filesystem::path cached_path(cache const &cache) const;
  };

  struct local_source {
    std::filesystem::path file_path;
  };

  using source_t = std::variant<builtin_source, remote_source, local_source>;

  recipe(std::string identity, source_t src);

  std::string const &identity() const { return identity_; }
  std::string_view namespace_name() const;
  std::string_view name() const;
  std::string_view version() const;

  source_t const &source() const { return source_; }

  std::vector<recipe const *> const &dependencies() const { return dependencies_; }
  void add_dependency(recipe const *dep);

 private:
  std::string identity_;
  source_t source_;
  std::vector<recipe const *> dependencies_;
};

}  // namespace envy

#pragma once

#include <string>
#include <string_view>

namespace envy {

struct recipe_spec;

// Canonical recipe key: "namespace.name@revision{opt1=val1,opt2=val2}"
// Immutable after construction, thread-safe for concurrent access
class recipe_key {
 public:
  explicit recipe_key(recipe_spec const &spec);
  explicit recipe_key(std::string_view canonical_or_identity);

  std::string const &canonical() const { return canonical_; }
  std::string_view identity() const { return identity_; }
  std::string_view namespace_() const { return ns_; }
  std::string_view name() const { return name_; }
  std::string_view revision() const { return revision_; }

  // Query can be partial: "name", "namespace.name", "name@revision", full canonical
  bool matches(std::string_view query) const;

  bool operator==(recipe_key const &other) const { return canonical_ == other.canonical_; }

  auto operator<=>(recipe_key const &other) const {
    return canonical_ <=> other.canonical_;
  }

  size_t hash() const { return hash_; }

 private:
  void parse_components();

  std::string canonical_;      // "namespace.name@revision{opt=val,...}"
  std::string_view identity_;  // "namespace.name@revision" (prefix of canonical_)
  std::string_view ns_;        // "namespace"
  std::string_view name_;      // "name"
  std::string_view revision_;  // "@revision" (includes '@')
  size_t hash_;                // Cached hash of canonical_
};

}  // namespace envy

// Hash specialization for std::unordered_map
template <>
struct std::hash<envy::recipe_key> {
  size_t operator()(envy::recipe_key const &k) const { return k.hash(); }
};

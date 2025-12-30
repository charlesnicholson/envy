#pragma once

#include <string>
#include <string_view>

namespace envy {

struct pkg_cfg;

// Canonical package key: "namespace.name@revision{opt1=val1,opt2=val2}"
// Immutable after construction, thread-safe for concurrent access
class pkg_key {
 public:
  explicit pkg_key(pkg_cfg const &cfg);
  explicit pkg_key(std::string_view canonical_or_identity);
  pkg_key(pkg_key const &other);
  pkg_key(pkg_key &&other) noexcept;
  pkg_key &operator=(pkg_key const &other);
  pkg_key &operator=(pkg_key &&other) noexcept;

  std::string const &canonical() const { return canonical_; }
  std::string_view identity() const { return identity_; }
  std::string_view namespace_() const { return ns_; }
  std::string_view name() const { return name_; }
  std::string_view revision() const { return revision_; }

  // Query can be partial: "name", "namespace.name", "name@revision", full canonical
  bool matches(std::string_view query) const;

  bool operator==(pkg_key const &other) const { return canonical_ == other.canonical_; }

  auto operator<=>(pkg_key const &other) const {
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
struct std::hash<envy::pkg_key> {
  size_t operator()(envy::pkg_key const &k) const { return k.hash(); }
};

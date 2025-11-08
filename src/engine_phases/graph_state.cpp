#include "graph_state.h"

#include "recipe_spec.h"

#include <algorithm>
#include <ranges>
#include <sstream>
#include <vector>

namespace envy {

std::string make_canonical_key(std::string const &identity,
                               std::unordered_map<std::string, lua_value> const &opts) {
  if (opts.empty()) { return identity; }

  std::vector<std::pair<std::string, std::string>> sorted;
  sorted.reserve(opts.size());
  for (auto const &[k, v] : opts) { sorted.emplace_back(k, serialize_option_table(v)); }
  std::ranges::sort(sorted);

  std::ostringstream oss;
  oss << identity << '{';
  bool first{ true };
  for (auto const &[k, v] : sorted) {
    if (!first) oss << ',';
    oss << k << '=' << v;
    first = false;
  }
  oss << '}';
  return oss.str();
}

}  // namespace envy

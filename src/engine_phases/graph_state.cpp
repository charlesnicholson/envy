#include "graph_state.h"

#include <algorithm>
#include <ranges>
#include <sstream>
#include <vector>

namespace envy {

std::string make_canonical_key(std::string const &identity,
                               std::unordered_map<std::string, std::string> const &opts) {
  if (opts.empty()) { return identity; }

  std::vector<std::pair<std::string, std::string>> sorted{ opts.begin(), opts.end() };
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

#include "lua_envy_dep_util.h"

namespace envy {

bool identity_matches(std::string const &dep_id, std::string const &query) {
  if (dep_id == query) { return true; }
  pkg_key const key{ dep_id };
  return key.matches(query);
}

bool dependency_reachable(pkg *from,
                          std::string const &query,
                          std::unordered_set<pkg *> &visited) {
  if (!visited.insert(from).second) { return false; }

  for (auto const &[dep_id, dep_info] : from->dependencies) {
    pkg *child{ dep_info.p };
    if (!child) { continue; }
    if (identity_matches(dep_id, query)) { return true; }
    if (dependency_reachable(child, query, visited)) { return true; }
  }

  return false;
}

bool strong_reachable(pkg *from,
                      std::string const &query,
                      pkg_phase &first_hop_needed_by,
                      std::optional<std::string> &matched_identity) {
  bool found{ false };

  for (auto const &[dep_id, dep_info] : from->dependencies) {
    pkg *child{ dep_info.p };
    if (!child) { continue; }

    bool reachable{ identity_matches(dep_id, query) };
    if (!reachable) {
      std::unordered_set<pkg *> visited;
      reachable = dependency_reachable(child, query, visited);
    }

    if (!reachable) { continue; }

    if (!found || dep_info.needed_by < first_hop_needed_by) {
      first_hop_needed_by = dep_info.needed_by;
      matched_identity = dep_id;
    }
    found = true;
  }

  return found;
}

bool strong_reachable(pkg *from,
                      std::string const &query,
                      pkg_phase &first_hop_needed_by) {
  std::optional<std::string> matched;
  return strong_reachable(from, query, first_hop_needed_by, matched);
}

}  // namespace envy

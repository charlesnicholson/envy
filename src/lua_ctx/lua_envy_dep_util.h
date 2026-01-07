#pragma once

#include "pkg.h"
#include "pkg_phase.h"

#include <string>
#include <unordered_set>

namespace envy {

// Check if target_identity is reachable through transitive dependencies
inline bool dependency_reachable(pkg *from,
                                 std::string const &target_identity,
                                 std::unordered_set<pkg *> &visited) {
  if (!visited.insert(from).second) { return false; }

  for (auto const &[dep_id, dep_info] : from->dependencies) {
    pkg *child{ dep_info.p };
    if (!child) { continue; }
    if (dep_id == target_identity) { return true; }
    if (dependency_reachable(child, target_identity, visited)) { return true; }
  }

  return false;
}

// Check if target_identity is reachable and find earliest needed_by phase
inline bool strong_reachable(pkg *from,
                             std::string const &target_identity,
                             pkg_phase &first_hop_needed_by) {
  bool found{ false };

  for (auto const &[dep_id, dep_info] : from->dependencies) {
    pkg *child{ dep_info.p };
    if (!child) { continue; }

    bool reachable{ dep_id == target_identity };
    if (!reachable) {
      std::unordered_set<pkg *> visited;
      reachable = dependency_reachable(child, target_identity, visited);
    }

    if (!reachable) { continue; }

    if (!found || dep_info.needed_by < first_hop_needed_by) {
      first_hop_needed_by = dep_info.needed_by;
    }
    found = true;
  }

  return found;
}

}  // namespace envy

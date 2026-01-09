#pragma once

#include "pkg.h"
#include "pkg_phase.h"

#include <optional>
#include <string>
#include <unordered_set>

namespace envy {

// Check if query matches dep_id using fuzzy matching
bool identity_matches(std::string const &dep_id, std::string const &query);

// Check if target_identity is reachable through transitive dependencies (with fuzzy
// matching)
bool dependency_reachable(pkg *from,
                          std::string const &query,
                          std::unordered_set<pkg *> &visited);

// Check if target_identity is reachable (with fuzzy matching) and find earliest needed_by
// phase. Returns the matched canonical identity via out parameter if found.
bool strong_reachable(pkg *from,
                      std::string const &query,
                      pkg_phase &first_hop_needed_by,
                      std::optional<std::string> &matched_identity);

// Overload without matched_identity output parameter
bool strong_reachable(pkg *from, std::string const &query, pkg_phase &first_hop_needed_by);

}  // namespace envy

#pragma once

#include <string_view>

namespace envy {

// Returns true if `candidate` is a strictly newer semver than `current`.
// Returns true if `current` fails to parse (treat corrupt/missing as "nothing").
// Returns false if `candidate` fails to parse (don't write garbage).
// Returns false if equal.
bool version_is_newer(std::string_view candidate, std::string_view current);

}  // namespace envy

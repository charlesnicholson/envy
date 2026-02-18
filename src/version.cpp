#include "version.h"

#include "semver.hpp"

#include <string_view>

namespace envy {

namespace {

std::string_view trim(std::string_view s) {
  auto const start{s.find_first_not_of(" \t\n\r")};
  if (start == std::string_view::npos) { return {}; }
  return s.substr(start, s.find_last_not_of(" \t\n\r") - start + 1);
}

}  // namespace

bool version_is_newer(std::string_view candidate, std::string_view current) {
  candidate = trim(candidate);
  current = trim(current);

  semver::version<> cand;
  if (!semver::parse(candidate, cand)) { return false; }

  semver::version<> cur;
  if (!semver::parse(current, cur)) { return true; }

  return cand > cur;
}

}  // namespace envy

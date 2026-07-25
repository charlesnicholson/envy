#include "envy_release.h"

#include "util.h"

#include <sstream>

namespace envy {

bool envy_release_version_is_valid(std::string_view version) {
  if (version.empty()) { return false; }
  // ASCII-only: version becomes the envy/<version> cache path component.
  for (char c : version) {
    if (!util_ascii_is_alnum(c) && c != '.' && c != '-' && c != '_') { return false; }
  }
  return true;
}

std::string_view envy_release_archive_ext(std::string_view os) {
  return os == "windows" ? ".zip" : ".tar.gz";
}

std::string envy_release_archive_name(std::string_view os, std::string_view arch) {
  std::ostringstream ss;
  ss << "envy-" << os << '-' << arch << envy_release_archive_ext(os);
  return ss.str();
}

std::string envy_release_url(std::string_view mirror_base,
                             std::string_view version,
                             std::string_view os,
                             std::string_view arch) {
  std::ostringstream ss;
  ss << mirror_base << "/v" << version << '/' << envy_release_archive_name(os, arch);
  return ss.str();
}

}  // namespace envy

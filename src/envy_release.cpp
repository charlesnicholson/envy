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

void envy_release_validate_mirror(std::string_view mirror, std::string_view op) {
  auto const reject{ [&](std::string_view what) {
    std::ostringstream msg;
    msg << op << ": mirror contains " << what
        << ", which cannot be represented in a manifest directive or bootstrap script: '"
        << mirror << "'";
    throw std::runtime_error(msg.str());
  } };

  if (mirror.empty()) { reject("nothing (empty value)"); }

  for (unsigned char const c : mirror) {
    switch (c) {
      case '"': reject("a double quote"); break;
      case '\\': reject("a backslash"); break;
      case '!': reject("an exclamation mark (batch delayed expansion)"); break;
      case '\n': reject("a newline"); break;
      case '\r': reject("a carriage return"); break;
      default:
        if (c < 0x20 || c == 0x7F) { reject("a control character"); }
        break;
    }
  }
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

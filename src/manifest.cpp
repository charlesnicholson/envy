#include "manifest.h"

namespace envy {

std::optional<std::filesystem::path> manifest::discover() {
  namespace fs = std::filesystem;

  auto cur{ fs::current_path() };

  for (;;) {
    auto const manifest_path{ cur / "envy.lua" };
    if (fs::exists(manifest_path)) { return manifest_path; }

    auto const git_path{ cur / ".git" };
    if (fs::exists(git_path) && fs::is_directory(git_path)) { return std::nullopt; }

    auto const parent{ cur.parent_path() };
    if (parent == cur) { return std::nullopt; }

    cur = parent;
  }
}

manifest manifest::load(std::filesystem::path const &path) {
  // TODO: Implement Lua loading
  manifest m;
  m.manifest_path = path;
  return m;
}

bool operator==(package_source const &lhs, package_source const &rhs) {
  // TODO: Implement deep comparison
  return lhs.recipe == rhs.recipe && lhs.source.index() == rhs.source.index() &&
         lhs.options == rhs.options;
}

}  // namespace envy

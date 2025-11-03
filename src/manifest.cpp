#include "manifest.h"

#include "lua_util.h"

#include <stdexcept>

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

manifest manifest::load(char const *script, std::filesystem::path const &manifest_path) {
  auto state{ lua_make() };
  if (!state) { throw std::runtime_error("Failed to create Lua state"); }

  lua_add_envy(state);

  if (!lua_run_string(state, script)) {
    throw std::runtime_error("Failed to execute manifest script");
  }

  manifest m;
  m.manifest_path = manifest_path;

  auto packages{ lua_global_to_array(state.get(), "packages") };
  if (!packages) { throw std::runtime_error("Manifest must define 'packages' global"); }

  for (auto const &package : *packages) {
    m.packages.push_back(recipe::parse(package, manifest_path));
  }

  return m;
}

}  // namespace envy

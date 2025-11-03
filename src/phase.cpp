#include "phase.h"

#include <array>

namespace envy {

namespace {

// Enum-to-string mapping (order must match phase enum in phase.h)
constinit std::array<std::string_view, 7> const phase_name_table{{
    "recipe_fetch",  // phase::recipe_fetch
    "check",         // phase::asset_check
    "fetch",         // phase::asset_fetch
    "stage",         // phase::asset_stage
    "build",         // phase::asset_build
    "install",       // phase::asset_install
    "deploy",        // phase::asset_deploy
}};

}  // namespace

std::string_view phase_name(phase p) {
  return phase_name_table[static_cast<std::size_t>(p)];
}

std::optional<phase> phase_parse(std::string_view name) {
  for (std::size_t i{}; i < phase_name_table.size(); ++i) {
    if (phase_name_table[i] == name) { return static_cast<phase>(i); }
  }
  return std::nullopt;
}

}  // namespace envy

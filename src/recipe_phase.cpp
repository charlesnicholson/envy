#include "recipe_phase.h"

#include <algorithm>
#include <array>

namespace envy {

namespace {

// Enum-to-string mapping (order must match recipe_phase enum in recipe_phase.h)
// Index = enum value, so none (-1) and completion (7) handled specially
constinit std::array<std::string_view, 7> const recipe_phase_name_table{ {
    "recipe_fetch",  // recipe_phase::recipe_fetch (0)
    "check",         // recipe_phase::asset_check (1)
    "fetch",         // recipe_phase::asset_fetch (2)
    "stage",         // recipe_phase::asset_stage (3)
    "build",         // recipe_phase::asset_build (4)
    "install",       // recipe_phase::asset_install (5)
    "deploy",        // recipe_phase::asset_deploy (6)
} };

}  // namespace

std::string_view recipe_phase_name(recipe_phase p) {
  if (p == recipe_phase::none) { return "none"; }
  if (p == recipe_phase::completion) { return "completion"; }
  auto const idx{ static_cast<std::size_t>(p) };
  if (idx >= recipe_phase_name_table.size()) { return "unknown"; }
  return recipe_phase_name_table[idx];
}

std::optional<recipe_phase> recipe_phase_parse(std::string_view name) {
  if (name == "none") { return recipe_phase::none; }
  if (name == "completion") { return recipe_phase::completion; }

  if (auto it{ std::ranges::find(recipe_phase_name_table, name) };
      it != recipe_phase_name_table.end()) {
    return static_cast<recipe_phase>(std::distance(recipe_phase_name_table.begin(), it));
  }
  return std::nullopt;
}

}  // namespace envy

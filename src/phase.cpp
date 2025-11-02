#include "phase.h"

#include <array>

namespace envy {

namespace {

constinit std::array<std::string_view, 7> const phase_name_table{{
    [static_cast<std::size_t>(phase::recipe_fetch)] = "recipe_fetch",
    [static_cast<std::size_t>(phase::asset_check)] = "check",
    [static_cast<std::size_t>(phase::asset_fetch)] = "fetch",
    [static_cast<std::size_t>(phase::asset_stage)] = "stage",
    [static_cast<std::size_t>(phase::asset_build)] = "build",
    [static_cast<std::size_t>(phase::asset_install)] = "install",
    [static_cast<std::size_t>(phase::asset_deploy)] = "deploy",
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

#pragma once

#include <optional>
#include <string_view>

namespace envy {

enum class recipe_phase : int {
  none = -1,  // Not started yet
  recipe_fetch = 0,
  asset_check = 1,
  asset_fetch = 2,
  asset_stage = 3,
  asset_build = 4,
  asset_install = 5,
  asset_deploy = 6,
  completion = 7,  // All phases complete
};

constexpr int recipe_phase_count = 9;  // none through completion

std::string_view recipe_phase_name(recipe_phase p);
std::optional<recipe_phase> recipe_phase_parse(std::string_view name);

}  // namespace envy

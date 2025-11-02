#pragma once

#include <optional>
#include <string_view>

namespace envy {

enum class phase {
  recipe_fetch,
  asset_check,
  asset_fetch,
  asset_stage,
  asset_build,
  asset_install,
  asset_deploy
};

std::string_view phase_name(phase p);
std::optional<phase> phase_parse(std::string_view name);

}  // namespace envy

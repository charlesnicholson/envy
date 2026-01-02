#pragma once

#include <optional>
#include <string_view>

namespace envy {

enum class pkg_phase : int {
  none = -1,  // Not started yet
  spec_fetch = 0,
  pkg_check = 1,
  pkg_fetch = 2,
  pkg_stage = 3,
  pkg_build = 4,
  pkg_install = 5,
  completion = 6,  // All phases complete
};

constexpr int pkg_phase_count = 8;  // none through completion

std::string_view pkg_phase_name(pkg_phase p);
std::optional<pkg_phase> pkg_phase_parse(std::string_view name);

}  // namespace envy

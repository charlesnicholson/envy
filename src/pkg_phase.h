#pragma once

#include <optional>
#include <string_view>

namespace envy {

enum class pkg_phase : int {
  none = -1,  // Not started yet
  spec_fetch = 0,
  pkg_check = 1,
  pkg_import = 2,
  pkg_fetch = 3,
  pkg_stage = 4,
  pkg_build = 5,
  pkg_install = 6,
  completion = 7,  // All phases complete
};

constexpr int pkg_phase_count = 9;  // none through completion

std::string_view pkg_phase_name(pkg_phase p);
std::optional<pkg_phase> pkg_phase_parse(std::string_view name);

}  // namespace envy

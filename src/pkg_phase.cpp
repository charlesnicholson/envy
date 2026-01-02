#include "pkg_phase.h"

#include <algorithm>
#include <array>

namespace envy {

namespace {

// Enum-to-string mapping (order must match pkg_phase enum in pkg_phase.h)
// Index = enum value, so none (-1) and completion (6) handled specially
constinit std::array<std::string_view, 6> const pkg_phase_name_table{ {
    "spec_fetch",  // pkg_phase::spec_fetch (0)
    "check",       // pkg_phase::pkg_check (1)
    "fetch",       // pkg_phase::pkg_fetch (2)
    "stage",       // pkg_phase::pkg_stage (3)
    "build",       // pkg_phase::pkg_build (4)
    "install",     // pkg_phase::pkg_install (5)
} };

}  // namespace

std::string_view pkg_phase_name(pkg_phase p) {
  if (p == pkg_phase::none) { return "none"; }
  if (p == pkg_phase::completion) { return "completion"; }
  auto const idx{ static_cast<std::size_t>(p) };
  if (idx >= pkg_phase_name_table.size()) { return "unknown"; }
  return pkg_phase_name_table[idx];
}

std::optional<pkg_phase> pkg_phase_parse(std::string_view name) {
  if (name == "none") { return pkg_phase::none; }
  if (name == "completion") { return pkg_phase::completion; }

  if (auto it{ std::ranges::find(pkg_phase_name_table, name) };
      it != pkg_phase_name_table.end()) {
    return static_cast<pkg_phase>(std::distance(pkg_phase_name_table.begin(), it));
  }
  return std::nullopt;
}

}  // namespace envy

#pragma once

namespace envy {

enum class recipe_phase : int {
  none = -1,        // Not started yet
  recipe_fetch = 0,
  check = 1,
  fetch = 2,
  stage = 3,
  build = 4,
  install = 5,
  deploy = 6,
  completion = 7,
};

constexpr int phase_count = 8;

}  // namespace envy

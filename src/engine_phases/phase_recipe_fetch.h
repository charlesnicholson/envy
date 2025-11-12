#pragma once

#include "../graph_state.h"
#include "recipe_spec.h"

#include <string>
#include <unordered_set>

namespace envy {

void run_recipe_fetch_phase(recipe_spec const &spec,
                            std::string const &key,
                            graph_state &state,
                            std::unordered_set<std::string> const &ancestors);

}  // namespace envy

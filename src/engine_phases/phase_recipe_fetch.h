#pragma once

#include "graph_state.h"

#include <unordered_set>

namespace envy {

struct recipe;
void run_recipe_fetch_phase(recipe *r,
                            graph_state &state,
                            std::unordered_set<std::string> const &ancestors);

}  // namespace envy

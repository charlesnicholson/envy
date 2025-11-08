#pragma once

#include "graph_state.h"
#include "recipe_spec.h"

#include <string>
#include <unordered_set>

namespace envy {

void create_recipe_nodes(std::string const &key,
                         recipe_spec const &spec,
                         graph_state &state,
                         std::unordered_set<std::string> const &ancestors = {});

}  // namespace envy

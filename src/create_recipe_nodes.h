#pragma once

#include "graph_state.h"
#include "recipe_spec.h"

#include <string>
#include <unordered_set>

namespace envy {

struct recipe;

// Creates a complete pipeline for the given recipe spec
recipe *create_recipe_nodes(recipe_spec const &spec,
                            graph_state &state,
                            std::unordered_set<std::string> const &ancestors = {});

}  // namespace envy

#pragma once

#include "graph_state.h"

namespace envy {

struct recipe;
void run_check_phase(recipe *r, graph_state &state);

}  // namespace envy

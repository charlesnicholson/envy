#pragma once

#include "graph_state.h"

namespace envy {

struct recipe;
void run_install_phase(recipe *r, graph_state &state);

}  // namespace envy

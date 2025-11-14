#pragma once

#include "graph_state.h"

namespace envy {

struct recipe;
void run_fetch_phase(recipe *r, graph_state &state);

}  // namespace envy

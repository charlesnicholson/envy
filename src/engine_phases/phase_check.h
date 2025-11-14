#pragma once

#include "graph_state.h"

struct lua_State;

namespace envy {

struct recipe;
void run_check_phase(recipe *r, graph_state &state);
bool recipe_has_check_verb(recipe *r, lua_State *lua);

}  // namespace envy

#pragma once

struct lua_State;

namespace envy {

class engine;

struct recipe;
void run_check_phase(recipe *r, engine &eng);
bool recipe_has_check_verb(recipe *r, lua_State *lua);

}  // namespace envy

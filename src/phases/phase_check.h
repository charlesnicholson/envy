#pragma once

#include "sol/sol.hpp"

namespace envy {

class engine;
struct recipe;

void run_check_phase(recipe *r, engine &eng);
bool recipe_has_check_verb(recipe *r, sol::state_view lua);

}  // namespace envy

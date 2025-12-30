#pragma once

#include "sol/sol.hpp"

namespace envy {

class engine;
struct pkg;

void run_check_phase(pkg *p, engine &eng);
bool pkg_has_check_verb(pkg *p, sol::state_view lua);

}  // namespace envy

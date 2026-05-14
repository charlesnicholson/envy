#pragma once

#include "sol/sol.hpp"

namespace envy {

class engine;
struct pkg;

void run_check_phase(pkg *p, engine &eng);

}  // namespace envy

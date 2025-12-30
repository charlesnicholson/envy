#pragma once

namespace envy {

class engine;

struct pkg;
void run_build_phase(pkg *p, engine &eng);

}  // namespace envy

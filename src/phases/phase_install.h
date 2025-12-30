#pragma once

namespace envy {

class engine;

struct pkg;
void run_install_phase(pkg *p, engine &eng);

}  // namespace envy

#pragma once

namespace envy {

class engine;

struct recipe;
void run_install_phase(recipe *r, engine &eng);

}  // namespace envy

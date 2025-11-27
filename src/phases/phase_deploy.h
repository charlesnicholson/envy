#pragma once

namespace envy {

class engine;

struct recipe;
void run_deploy_phase(recipe *r, engine &eng);

}  // namespace envy

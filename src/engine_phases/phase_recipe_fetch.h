#pragma once

namespace envy {

class engine;
struct recipe;

void run_recipe_fetch_phase(recipe *r, engine &eng);

}  // namespace envy

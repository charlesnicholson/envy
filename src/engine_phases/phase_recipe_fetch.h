#pragma once



#include <unordered_set>

namespace envy {

class engine;

struct recipe;
void run_recipe_fetch_phase(recipe *r,
                            engine &eng,
                            std::unordered_set<std::string> const &ancestors);

}  // namespace envy

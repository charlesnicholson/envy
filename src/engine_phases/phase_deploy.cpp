#include "phase_deploy.h"

#include "recipe.h"
#include "tui.h"

namespace envy {

void run_deploy_phase(recipe *r, engine &eng) {
  std::string const key{ r->spec.format_key() };
  tui::trace("phase deploy START [%s]", key.c_str());
  // TODO: Implement deploy logic
}

}  // namespace envy

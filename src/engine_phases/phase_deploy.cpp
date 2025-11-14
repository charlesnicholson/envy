#include "phase_deploy.h"

#include "recipe.h"
#include "tui.h"

namespace envy {

void run_deploy_phase(recipe *r, graph_state &state) {
  std::string const key{ r->spec.format_key() };
  (void)state;
  tui::trace("phase deploy START [%s]", key.c_str());
  trace_on_exit trace_end{ "phase deploy END [" + key + "]" };
  // TODO: Implement deploy logic
}

}  // namespace envy

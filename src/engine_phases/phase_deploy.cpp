#include "phase_deploy.h"

#include "tui.h"

namespace envy {

void run_deploy_phase(std::string const &key, graph_state &state) {
  (void)state;
  tui::trace("phase deploy START %s", key.c_str());
  trace_on_exit trace_end{ "phase deploy END " + key };
  // TODO: Implement deploy logic
}

}  // namespace envy

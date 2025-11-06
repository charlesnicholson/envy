#include "phase_stage.h"

#include "tui.h"

namespace envy {

void run_stage_phase(std::string const &key, graph_state &state) {
  (void)state;
  tui::trace("phase stage START %s", key.c_str());
  trace_on_exit trace_end{ "phase stage END " + key };
  // TODO: Implement stage logic
}

}  // namespace envy

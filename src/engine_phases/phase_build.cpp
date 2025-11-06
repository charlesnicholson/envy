#include "phase_build.h"

#include "tui.h"

namespace envy {

void run_build_phase(std::string const &key, graph_state &state) {
  (void)state;
  tui::trace("phase build START %s", key.c_str());
  trace_on_exit trace_end{ "phase build END " + key };
  // TODO: Implement build logic
}

}  // namespace envy

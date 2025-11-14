#include "phase_completion.h"

#include "recipe.h"
#include "tui.h"

namespace envy {

void run_completion_phase(recipe *r, graph_state &state) {
  std::string const key{ r->spec.format_key() };
  tui::trace("phase completion START [%s]", key.c_str());
  trace_on_exit trace_end{ "phase completion END [" + key + "]" };

  if (!r->asset_path.empty()) {
    r->result_hash = r->canonical_identity_hash;

    tui::trace("phase completion: result_hash=%s for %s",
               r->result_hash.c_str(),
               r->spec.identity.c_str());
  } else {  // Programmatic package - no cached artifacts
    r->result_hash = "programmatic";
    tui::trace("phase completion: no asset_path for %s (programmatic package)",
               r->spec.identity.c_str());
  }
}

}  // namespace envy

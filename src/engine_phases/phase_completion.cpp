#include "phase_completion.h"

#include "recipe.h"
#include "trace.h"
#include "tui.h"

#include <chrono>

namespace envy {

void run_completion_phase(recipe *r, engine &eng) {
  phase_trace_scope const phase_scope{ r->spec.identity,
                                       recipe_phase::completion,
                                       std::chrono::steady_clock::now() };

  if (!r->asset_path.empty()) {
    r->result_hash = r->canonical_identity_hash;

    tui::debug("phase completion: result_hash=%s for %s",
               r->result_hash.c_str(),
               r->spec.identity.c_str());
  } else {  // Programmatic package - no cached artifacts
    r->result_hash = "programmatic";
    tui::debug("phase completion: no asset_path for %s (programmatic package)",
               r->spec.identity.c_str());
  }
}

}  // namespace envy

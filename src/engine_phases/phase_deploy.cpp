#include "phase_deploy.h"

#include "recipe.h"
#include "trace.h"
#include "tui.h"

#include <chrono>

namespace envy {

void run_deploy_phase(recipe *r, engine &eng) {
  phase_trace_scope const phase_scope{ r->spec->identity,
                                       recipe_phase::asset_deploy,
                                       std::chrono::steady_clock::now() };
  // TODO: Implement deploy logic
}

}  // namespace envy

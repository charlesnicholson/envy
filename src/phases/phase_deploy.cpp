#include "phase_deploy.h"

#include "pkg.h"
#include "trace.h"

#include <chrono>

namespace envy {

void run_deploy_phase(pkg *p, engine &eng) {
  phase_trace_scope const phase_scope{ p->cfg->identity,
                                       pkg_phase::pkg_deploy,
                                       std::chrono::steady_clock::now() };
  // TODO: Implement deploy logic
}

}  // namespace envy

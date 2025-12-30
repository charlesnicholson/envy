#include "phase_completion.h"

#include "engine.h"
#include "pkg.h"
#include "trace.h"
#include "tui.h"

#include <chrono>

namespace envy {

void run_completion_phase(pkg *p, engine &eng) {
  phase_trace_scope const phase_scope{ p->cfg->identity,
                                       pkg_phase::completion,
                                       std::chrono::steady_clock::now() };

  if (p->type == pkg_type::CACHE_MANAGED) {
    p->result_hash = p->canonical_identity_hash;

    tui::debug("phase completion: result_hash=%s for %s",
               p->result_hash.c_str(),
               p->cfg->identity.c_str());
  } else {
    p->result_hash = "user-managed";
    tui::debug("phase completion: no pkg_path for %s (user-managed package)",
               p->cfg->identity.c_str());
  }

  if (p->tui_section && tui::section_has_content(p->tui_section)) {
    tui::section_set_content(
        p->tui_section,
        tui::section_frame{ .label = "[" + p->cfg->identity + "]",
                            .content = tui::static_text_data{ .text = "done" } });
  }
}

}  // namespace envy

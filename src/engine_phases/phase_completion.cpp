#include "phase_completion.h"

#include "tui.h"

#include <stdexcept>

namespace envy {

void run_completion_phase(std::string const &key, graph_state &state) {
  tui::trace("phase completion START %s", key.c_str());
  trace_on_exit trace_end{ "phase completion END " + key };

  typename decltype(state.recipes)::accessor acc;
  if (!state.recipes.find(acc, key)) {
    throw std::runtime_error("Recipe not found in completion phase: " + key);
  }

  if (!acc->second.asset_path.empty()) {
    auto const path_str{ acc->second.asset_path.string() };
    acc->second.result_hash =
        path_str.length() >= 16 ? path_str.substr(path_str.length() - 16) : path_str;

    tui::trace("phase completion: computed result_hash=%s for %s",
               acc->second.result_hash.c_str(),
               key.c_str());
  } else {
    // Programmatic package - no cached artifacts
    acc->second.result_hash = "programmatic";
    tui::trace("phase completion: no asset_path for %s (programmatic package)",
               key.c_str());
  }

  acc->second.completed = true;
}

}  // namespace envy

#include "engine.h"

#include "create_recipe_nodes.h"
#include "graph_state.h"
#include "recipe.h"
#include "tui.h"

#include <vector>

namespace envy {

// Helper to format a canonical key for result map (identity or identity{options})
static std::string format_result_key(recipe const *r) { return r->spec.format_key(); }

recipe_result_map_t engine_run(std::vector<recipe_spec> const &roots,
                               cache &cache_,
                               manifest const &manifest_) {
  tbb::flow::graph flow_graph;
  graph_state state{ .graph = flow_graph, .cache_ = cache_, .manifest_ = &manifest_ };

  // Create recipe graphs for all roots and collect pointers
  std::vector<recipe *> root_recipes;
  for (auto const &spec : roots) {
    recipe *r = create_recipe_nodes(spec, state);
    root_recipes.push_back(r);
    r->recipe_fetch_node->try_put(tbb::flow::continue_msg{});
  }

  flow_graph.wait_for_all();

  return [&]() {
    recipe_result_map_t result;
    std::lock_guard<std::mutex> lock(state.recipes_mutex);
    for (auto const &r : state.recipes) {
      std::string const key{ format_result_key(r.get()) };
      if (r->result_hash.empty()) {
        tui::warn("Recipe %s has EMPTY result_hash after wait_for_all()", key.c_str());
        tui::warn("  asset_path: %s", r->asset_path.string().c_str());
        tui::warn("  has lock: %s", r->lock ? "yes" : "no");
      }
      result[key] =
          recipe_result{ .result_hash = r->result_hash, .asset_path = r->asset_path };
    }
    return result;
  }();
}

}  // namespace envy

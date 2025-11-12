#include "engine.h"

#include "create_recipe_nodes.h"
#include "graph_state.h"
#include "tui.h"

namespace envy {

recipe_result_map_t engine_run(std::vector<recipe_spec> const &roots,
                               cache &cache_,
                               manifest const &manifest_) {
  tbb::flow::graph flow_graph;
  graph_state state{ .graph = flow_graph, .cache_ = cache_, .manifest_ = &manifest_ };

  for (auto const &cfg : roots) {
    auto const key{ make_canonical_key(cfg.identity, cfg.options) };
    create_recipe_nodes(key, cfg, state);
    state.triggered.insert(key);

    typename decltype(state.recipes)::const_accessor acc;
    if (state.recipes.find(acc, key)) {
      acc->second.recipe_fetch_node->try_put(tbb::flow::continue_msg{});
    }
  }

  flow_graph.wait_for_all();

  recipe_result_map_t result;
  for (auto const &entry : state.recipes) {
    auto const &key{ entry.first };
    typename decltype(state.recipes)::const_accessor acc;
    state.recipes.find(acc, key);
    if (acc->second.result_hash.empty()) {
      tui::warn("Recipe %s has EMPTY result_hash after wait_for_all()", key.c_str());
      tui::warn("  asset_path: %s", acc->second.asset_path.string().c_str());
      tui::warn("  has lock: %s", acc->second.lock ? "yes" : "no");
    }
    result[key] = recipe_result{ .result_hash = acc->second.result_hash,
                                 .asset_path = acc->second.asset_path };
  }
  return result;
}

}  // namespace envy

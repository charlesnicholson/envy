#include "create_recipe_nodes.h"

#include "engine_phases/phase_build.h"
#include "engine_phases/phase_check.h"
#include "engine_phases/phase_completion.h"
#include "engine_phases/phase_deploy.h"
#include "engine_phases/phase_fetch.h"
#include "engine_phases/phase_install.h"
#include "engine_phases/phase_recipe_fetch.h"
#include "engine_phases/phase_stage.h"
#include "recipe.h"

#include <memory>
#include <stdexcept>

namespace envy {

recipe *create_recipe_nodes(recipe_spec const &spec,
                            graph_state &state,
                            std::unordered_set<std::string> const &ancestors) {
  // Cycle detection using identity
  if (ancestors.contains(spec.identity)) {
    throw std::runtime_error("Cycle detected: " + spec.identity + " depends on itself");
  }

  if (spec.identity.starts_with("local.") && !spec.is_local()) {
    throw std::runtime_error("Recipe 'local.*' must have local source: " + spec.identity);
  }

  auto r_ptr = std::make_unique<recipe>();
  recipe *r = r_ptr.get();

  r->spec = spec;

  // Create all 8 phase nodes, capturing recipe pointer
  r->recipe_fetch_node =
      std::make_shared<tbb::flow::continue_node<tbb::flow::continue_msg>>(
          state.graph,
          [r, &state, ancestors](tbb::flow::continue_msg const &) {
            run_recipe_fetch_phase(r, state, ancestors);
          });

  r->check_node = std::make_shared<tbb::flow::continue_node<tbb::flow::continue_msg>>(
      state.graph,
      [r, &state](tbb::flow::continue_msg const &) { run_check_phase(r, state); });

  r->fetch_node = std::make_shared<tbb::flow::continue_node<tbb::flow::continue_msg>>(
      state.graph,
      [r, &state](tbb::flow::continue_msg const &) { run_fetch_phase(r, state); });

  r->stage_node = std::make_shared<tbb::flow::continue_node<tbb::flow::continue_msg>>(
      state.graph,
      [r, &state](tbb::flow::continue_msg const &) { run_stage_phase(r, state); });

  r->build_node = std::make_shared<tbb::flow::continue_node<tbb::flow::continue_msg>>(
      state.graph,
      [r, &state](tbb::flow::continue_msg const &) { run_build_phase(r, state); });

  r->install_node = std::make_shared<tbb::flow::continue_node<tbb::flow::continue_msg>>(
      state.graph,
      [r, &state](tbb::flow::continue_msg const &) { run_install_phase(r, state); });

  r->deploy_node = std::make_shared<tbb::flow::continue_node<tbb::flow::continue_msg>>(
      state.graph,
      [r, &state](tbb::flow::continue_msg const &) { run_deploy_phase(r, state); });

  r->completion_node = std::make_shared<tbb::flow::continue_node<tbb::flow::continue_msg>>(
      state.graph,
      [r, &state](tbb::flow::continue_msg const &) { run_completion_phase(r, state); });

  // Wire all static edges (complete 8-phase pipeline)
  tbb::flow::make_edge(*r->recipe_fetch_node, *r->check_node);
  tbb::flow::make_edge(*r->check_node, *r->fetch_node);
  tbb::flow::make_edge(*r->fetch_node, *r->stage_node);
  tbb::flow::make_edge(*r->stage_node, *r->build_node);
  tbb::flow::make_edge(*r->build_node, *r->install_node);
  tbb::flow::make_edge(*r->install_node, *r->deploy_node);
  tbb::flow::make_edge(*r->deploy_node, *r->completion_node);

  {
    std::lock_guard lock{ state.recipes_mutex };
    state.recipes.push_back(std::move(r_ptr));
  }

  return r;
}

}  // namespace envy

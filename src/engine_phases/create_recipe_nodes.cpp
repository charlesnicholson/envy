#include "create_recipe_nodes.h"

#include "phase_build.h"
#include "phase_check.h"
#include "phase_completion.h"
#include "phase_deploy.h"
#include "phase_fetch.h"
#include "phase_install.h"
#include "phase_recipe_fetch.h"
#include "phase_stage.h"

#include <stdexcept>

namespace envy {

void create_recipe_nodes(std::string const &key,
                         recipe_spec const &spec,
                         graph_state &state,
                         std::unordered_set<std::string> const &ancestors) {
  if (ancestors.contains(key)) {
    throw std::runtime_error("Cycle detected: " + key + " depends on itself");
  }

  {
    typename decltype(state.recipes)::const_accessor acc;
    if (state.recipes.find(acc, key)) { return; }
  }

  if (spec.identity.starts_with("local.") && !spec.is_local()) {
    throw std::runtime_error("Recipe 'local.*' must have local source: " + spec.identity);
  }

  auto recipe_fetch_node{
    std::make_shared<tbb::flow::continue_node<tbb::flow::continue_msg>>(
        state.graph,
        [spec, key, &state, ancestors](tbb::flow::continue_msg const &) {
          run_recipe_fetch_phase(spec, key, state, ancestors);
        })
  };

  auto check_node{ std::make_shared<tbb::flow::continue_node<tbb::flow::continue_msg>>(
      state.graph,
      [key, &state](tbb::flow::continue_msg const &) { run_check_phase(key, state); }) };

  auto fetch_node{ std::make_shared<tbb::flow::continue_node<tbb::flow::continue_msg>>(
      state.graph,
      [key, &state](tbb::flow::continue_msg const &) { run_fetch_phase(key, state); }) };

  auto stage_node{ std::make_shared<tbb::flow::continue_node<tbb::flow::continue_msg>>(
      state.graph,
      [key, &state](tbb::flow::continue_msg const &) { run_stage_phase(key, state); }) };

  auto build_node{ std::make_shared<tbb::flow::continue_node<tbb::flow::continue_msg>>(
      state.graph,
      [key, &state](tbb::flow::continue_msg const &) { run_build_phase(key, state); }) };

  auto install_node{ std::make_shared<tbb::flow::continue_node<tbb::flow::continue_msg>>(
      state.graph,
      [key, &state](tbb::flow::continue_msg const &) { run_install_phase(key, state); }) };

  auto deploy_node{ std::make_shared<tbb::flow::continue_node<tbb::flow::continue_msg>>(
      state.graph,
      [key, &state](tbb::flow::continue_msg const &) { run_deploy_phase(key, state); }) };

  auto completion_node{
    std::make_shared<tbb::flow::continue_node<tbb::flow::continue_msg>>(
        state.graph,
        [key, &state](tbb::flow::continue_msg const &) {
          run_completion_phase(key, state);
        })
  };

  tbb::flow::make_edge(*recipe_fetch_node, *check_node);
  tbb::flow::make_edge(*fetch_node, *stage_node);
  tbb::flow::make_edge(*stage_node, *build_node);
  tbb::flow::make_edge(*build_node, *install_node);
  tbb::flow::make_edge(*install_node, *deploy_node);

  {
    typename decltype(state.recipes)::accessor acc;
    if (state.recipes.insert(acc, key)) {
      acc->second.recipe_fetch_node = recipe_fetch_node;
      acc->second.check_node = check_node;
      acc->second.fetch_node = fetch_node;
      acc->second.stage_node = stage_node;
      acc->second.build_node = build_node;
      acc->second.install_node = install_node;
      acc->second.deploy_node = deploy_node;
      acc->second.completion_node = completion_node;
      acc->second.identity = spec.identity;
      acc->second.options = spec.options;
    }
  }
}

}  // namespace envy

#include "engine.h"

#include "phases/phase_build.h"
#include "phases/phase_check.h"
#include "phases/phase_completion.h"
#include "phases/phase_deploy.h"
#include "phases/phase_fetch.h"
#include "phases/phase_install.h"
#include "phases/phase_recipe_fetch.h"
#include "phases/phase_stage.h"
#include "recipe.h"
#include "recipe_key.h"
#include "recipe_phase.h"
#include "tui.h"

#include <algorithm>
#include <array>
#include <vector>

namespace envy {

namespace {

using phase_func_t = void (*)(recipe *, engine &);

constexpr std::array<phase_func_t, recipe_phase_count> phase_dispatch_table{
  run_recipe_fetch_phase,  // recipe_phase::recipe_fetch
  run_check_phase,         // recipe_phase::asset_check
  run_fetch_phase,         // recipe_phase::asset_fetch
  run_stage_phase,         // recipe_phase::asset_stage
  run_build_phase,         // recipe_phase::asset_build
  run_install_phase,       // recipe_phase::asset_install
  run_deploy_phase,        // recipe_phase::asset_deploy
  run_completion_phase,    // recipe_phase::completion
};

}  // namespace

void validate_dependency_cycle(std::string const &candidate_identity,
                               std::vector<std::string> const &ancestor_chain,
                               std::string const &current_identity,
                               std::string const &dependency_type) {
  if (current_identity == candidate_identity) {  // Check for self-loop
    throw std::runtime_error(dependency_type + " cycle detected: " + current_identity +
                             " -> " + candidate_identity);
  }

  for (size_t i = 0; i < ancestor_chain.size(); ++i) {  // cycle in ancestor chain
    if (ancestor_chain[i] == candidate_identity) {
      std::string cycle_path{ ancestor_chain[i] };
      for (size_t j{ i + 1 }; j < ancestor_chain.size(); ++j) {
        cycle_path += " -> " + ancestor_chain[j];
      }
      cycle_path += " -> " + current_identity + " -> " + candidate_identity;
      throw std::runtime_error(dependency_type + " cycle detected: " + cycle_path);
    }
  }
}

engine::engine(cache &cache, default_shell_cfg_t default_shell)
    : cache_(cache), default_shell_(default_shell) {}

engine::~engine() {
  for (auto &[key, ctx] : execution_ctxs_) {
    if (ctx->worker.joinable()) { ctx->worker.join(); }
  }
}

void engine::notify_all_global_locked() {
  std::lock_guard const lock(mutex_);
  cv_.notify_all();
}

engine::weak_resolution_result engine::resolve_weak_references() {
  weak_resolution_result result{};

  auto collect_unresolved = [this]() {
    std::vector<std::pair<recipe *, recipe::weak_reference *>> unresolved;
    std::lock_guard const lock(mutex_);
    for (auto &[key, r_ptr] : recipes_) {
      recipe *r{ r_ptr.get() };
      for (auto &wr : r->weak_references) {
        if (!wr.resolved) { unresolved.emplace_back(r, &wr); }
      }
    }
    return unresolved;
  };

  std::vector<std::string> ambiguity_messages;

  for (auto [r, wr] : collect_unresolved()) {
    auto const matches{ find_matches(wr->query) };

    if (matches.size() == 1) {  // Resolved to existing recipe
      recipe *dep{ matches[0] };
      wr->resolved = dep;
      ++result.resolved;

      // Wire dependency if not already present
      if (!r->dependencies.contains(dep->spec->identity)) {
        r->dependencies[dep->spec->identity] = { dep, wr->needed_by };
        ENVY_TRACE_DEPENDENCY_ADDED(r->spec->identity, dep->spec->identity, wr->needed_by);
      }

      // Ensure declared_dependencies includes resolved identity for ctx.asset validation
      if (std::ranges::find(r->declared_dependencies, dep->spec->identity) ==
          r->declared_dependencies.end()) {
        r->declared_dependencies.push_back(dep->spec->identity);
      }
      continue;
    }

    if (matches.size() > 1) {
      std::ostringstream oss;
      oss << "Reference '" << wr->query << "' in recipe '" << r->spec->identity
          << "' is ambiguous: ";
      for (size_t i = 0; i < matches.size(); ++i) {
        if (i) oss << ", ";
        oss << matches[i]->key.canonical();
      }
      ambiguity_messages.push_back(oss.str());
      continue;
    }

    if (wr->fallback) {  // No matches, instantiate weak fallbacks
      wr->fallback->parent = r->spec;

      recipe *dep{ ensure_recipe(wr->fallback.get()) };
      if (!r->dependencies.contains(dep->spec->identity)) {
        r->dependencies[dep->spec->identity] = { dep, wr->needed_by };
        ENVY_TRACE_DEPENDENCY_ADDED(r->spec->identity, dep->spec->identity, wr->needed_by);
      }

      if (std::ranges::find(r->declared_dependencies, dep->spec->identity) ==
          r->declared_dependencies.end()) {
        r->declared_dependencies.push_back(dep->spec->identity);
      }

      std::vector<std::string> child_chain{ get_execution_ctx(r).ancestor_chain };
      child_chain.push_back(r->spec->identity);
      start_recipe_thread(dep, recipe_phase::recipe_fetch, std::move(child_chain));

      wr->resolved = dep;
      wr->fallback.reset();
      ++result.fallbacks_started;
    }
  }

  // Final validation: any unresolved without fallback is an error
  std::vector<std::string> const missing_messages{ [&] {
    std::vector<std::string> msgs{ ambiguity_messages.begin(), ambiguity_messages.end() };
    for (auto [r, wr] : collect_unresolved()) {
      if (!wr->resolved && !wr->fallback) {
        msgs.push_back("Reference '" + wr->query + "' in recipe '" + r->spec->identity +
                       "' was not found");
      }
    }
    return msgs;
  }() };

  if (!missing_messages.empty()) {
    std::ostringstream oss;
    for (size_t i{ 0 }; i < missing_messages.size(); ++i) {
      if (i) oss << "\n";
      oss << missing_messages[i];
    }
    throw std::runtime_error(oss.str());
  }

  return result;
}

void engine::recipe_execution_ctx::set_target_phase(recipe_phase target) {
  recipe_phase current_target{ target_phase.load() };
  while (current_target < target) {
    if (target_phase.compare_exchange_weak(current_target, target)) {
      std::lock_guard const lock(mutex);
      cv.notify_one();
      return;
    }
  }
}

void engine::recipe_execution_ctx::start(recipe *r,
                                         engine *eng,
                                         std::vector<std::string> chain) {
  ancestor_chain = std::move(chain);
  worker = std::thread([r, eng] { eng->run_recipe_thread(r); });
}

recipe *engine::ensure_recipe(recipe_spec const *spec) {
  std::lock_guard const lock(mutex_);

  recipe_key const key(*spec);

  auto r{ std::unique_ptr<recipe>(new recipe{
      .key = key,
      .spec = spec,
      .lua = nullptr,
      .lock = nullptr,
      .declared_dependencies = {},
      .owned_dependency_specs = {},
      .dependencies = {},
      .weak_references = {},
      .canonical_identity_hash = key.canonical(),
      .asset_path = std::filesystem::path{},
      .result_hash = {},
      .cache_ptr = &cache_,
      .default_shell_ptr = &default_shell_,
  }) };

  auto const [it, inserted]{ recipes_.try_emplace(key, std::move(r)) };
  if (inserted) {
    execution_ctxs_[key] = std::make_unique<recipe_execution_ctx>();
    ENVY_TRACE_RECIPE_REGISTERED(spec->identity, key.canonical(), false);
  }
  return it->second.get();
}

recipe *engine::find_exact(recipe_key const &key) const {
  std::lock_guard const lock(mutex_);
  auto const it{ recipes_.find(key) };
  return (it != recipes_.end()) ? it->second.get() : nullptr;
}

std::vector<recipe *> engine::find_matches(std::string_view query) const {
  std::lock_guard const lock(mutex_);

  std::vector<recipe *> matches;
  for (auto const &[key, r] : recipes_) {
    if (key.matches(query)) { matches.push_back(r.get()); }
  }

  return matches;
}

engine::recipe_execution_ctx &engine::get_execution_ctx(recipe *r) {
  return get_execution_ctx(r->key);
}

engine::recipe_execution_ctx &engine::get_execution_ctx(recipe_key const &key) {
  std::lock_guard const lock(mutex_);
  auto const it{ execution_ctxs_.find(key) };
  if (it == execution_ctxs_.end()) {
    throw std::runtime_error("Recipe execution context not found: " + key.canonical());
  }
  return *it->second;
}

void engine::start_recipe_thread(recipe *r,
                                 recipe_phase initial_target,
                                 std::vector<std::string> ancestor_chain) {
  auto &ctx{ get_execution_ctx(r) };

  bool expected{ false };
  if (ctx.started.compare_exchange_strong(expected, true)) {  // set phase then start
    if (initial_target >= recipe_phase::recipe_fetch) { on_recipe_fetch_start(); }
    ctx.set_target_phase(initial_target);
    ENVY_TRACE_THREAD_START(r->spec->identity, initial_target);
    ctx.start(r, this, std::move(ancestor_chain));
  } else {  // already started, extend target if needed
    ctx.set_target_phase(initial_target);
  }
}

void engine::ensure_recipe_at_phase(recipe_key const &key, recipe_phase const target) {
  auto &ctx{ get_execution_ctx(key) };

  ctx.set_target_phase(target);  // Extend target if needed

  // Wait for recipe to reach target
  std::unique_lock lock(mutex_);
  cv_.wait(lock, [&ctx, target] { return ctx.current_phase >= target || ctx.failed; });

  if (ctx.failed) {
    std::lock_guard ctx_lock(ctx.mutex);
    std::string const msg{ ctx.error_message.empty() ? "Recipe failed: " + key.canonical()
                                                     : ctx.error_message };
    throw std::runtime_error(msg);
  }
}

void engine::wait_for_resolution_phase() {
  std::unique_lock lock(mutex_);
  cv_.wait(lock, [this] { return pending_recipe_fetches_ == 0; });
}

void engine::notify_phase_complete() { notify_all_global_locked(); }

void engine::on_recipe_fetch_start() { pending_recipe_fetches_.fetch_add(1); }

void engine::on_recipe_fetch_complete() {
  if (pending_recipe_fetches_.fetch_sub(1) == 1) { notify_all_global_locked(); }
}

void engine::process_fetch_dependencies(recipe *r,
                                        std::vector<std::string> const &ancestor_chain) {
  // Process fetch dependencies - added to dependencies map with needed_by=recipe_fetch
  // Existing phase loop wait logic handles blocking automatically
  for (auto &fetch_dep_spec : r->spec->source_dependencies) {
    fetch_dep_spec.parent = r->spec;  // Set parent pointer for custom fetch lookup

    validate_dependency_cycle(fetch_dep_spec.identity,
                              ancestor_chain,
                              r->spec->identity,
                              "Fetch dependency");

    recipe *fetch_dep{ ensure_recipe(&fetch_dep_spec) };

    // Add to dependencies map - phase loop will handle blocking at recipe_fetch
    r->dependencies[fetch_dep_spec.identity] = { fetch_dep, recipe_phase::recipe_fetch };
    ENVY_TRACE_DEPENDENCY_ADDED(r->spec->identity,
                                fetch_dep_spec.identity,
                                recipe_phase::recipe_fetch);

    // Build child ancestor chain (local to this thread path)
    std::vector<std::string> child_chain{ ancestor_chain };
    child_chain.push_back(r->spec->identity);

    start_recipe_thread(fetch_dep, recipe_phase::completion, std::move(child_chain));
  }
}

void engine::run_recipe_thread(recipe *r) {
  auto &ctx{ get_execution_ctx(r) };

  if (!r->spec->source_dependencies.empty()) {
    process_fetch_dependencies(r, ctx.ancestor_chain);
  }

  try {
    while (ctx.current_phase < recipe_phase::completion) {
      recipe_phase const target{ ctx.target_phase };
      recipe_phase const current{ ctx.current_phase };

      if (current >= target) {  // Check if we've reached target
        std::unique_lock lock(ctx.mutex);
        ctx.cv.wait(lock,  // Wait for target extension
                    [&ctx, current] { return ctx.target_phase > current || ctx.failed; });

        if (ctx.target_phase == current || ctx.failed) break;
        ENVY_TRACE_TARGET_EXTENDED(r->spec->identity, current, ctx.target_phase.load());
      }

      recipe_phase const next{ static_cast<recipe_phase>(static_cast<int>(current) + 1) };
      if (static_cast<int>(next) < 0 || static_cast<int>(next) >= recipe_phase_count) {
        throw std::runtime_error("Invalid phase: " +
                                 std::to_string(static_cast<int>(next)));
      }

      // Wait for dependencies that are needed by this phase
      // If dependency has needed_by=build, we must wait for it before entering build phase
      for (auto const &[dep_identity, dep_info] : r->dependencies) {
        if (next >= dep_info.needed_by) {
          // Dependency is needed by this or earlier phase, ensure it's fully complete
          ENVY_TRACE_PHASE_BLOCKED(r->spec->identity,
                                   next,
                                   dep_identity,
                                   recipe_phase::completion);
          ensure_recipe_at_phase(dep_info.recipe_ptr->key, recipe_phase::completion);
          ENVY_TRACE_PHASE_UNBLOCKED(r->spec->identity, next, dep_identity);
        }
      }

      phase_dispatch_table[static_cast<int>(next)](r, *this);

      ctx.current_phase = next;
      notify_phase_complete();
    }
    ENVY_TRACE_THREAD_COMPLETE(r->spec->identity, ctx.current_phase);
  } catch (...) {  // Log the error (inspect exception type to get message if available)
    std::string error_msg;
    try {
      throw;  // rethrow to inspect
    } catch (std::exception const &e) {
      error_msg = e.what();
      tui::error("Recipe thread failed: %s", error_msg.c_str());
    } catch (...) {
      error_msg = "unknown exception";
      tui::error("Recipe thread failed with unknown exception");
    }

    {
      std::lock_guard lock(ctx.mutex);
      ctx.error_message = std::move(error_msg);
    }

    ctx.failed = true;
    if (ctx.current_phase <= recipe_phase::recipe_fetch) { on_recipe_fetch_complete(); }
    notify_all_global_locked();
  }
}

recipe_result_map_t engine::run_full(std::vector<recipe_spec const *> const &roots) {
  resolve_graph(roots);

  for (auto &[key, ctx] : execution_ctxs_) {  // Launch all recipes running to completion
    ctx->set_target_phase(recipe_phase::completion);
  }

  tui::debug("engine: joining %zu recipe threads", execution_ctxs_.size());
  for (auto &[key, ctx] : execution_ctxs_) {  // Wait for all recipes to complete
    if (ctx->worker.joinable()) { ctx->worker.join(); }
  }
  tui::debug("engine: all recipe threads joined");

  for (auto const &[key, ctx] : execution_ctxs_) {  // Check for failures
    if (ctx->failed) {
      std::lock_guard ctx_lock(ctx->mutex);
      std::string const msg{ ctx->error_message.empty()
                                 ? "Recipe failed: " + key.canonical()
                                 : ctx->error_message };
      throw std::runtime_error(msg);
    }
  }

  recipe_result_map_t results;
  for (auto const &[key, r] : recipes_) {
    results[r->key.canonical()] = { r->result_hash, r->asset_path };
  }
  return results;
}

void engine::resolve_graph(std::vector<recipe_spec const *> const &roots) {
  for (auto const *spec : roots) {
    recipe *const r{ ensure_recipe(spec) };
    tui::debug("engine: resolve_graph start thread for %s", spec->identity.c_str());
    start_recipe_thread(r, recipe_phase::recipe_fetch);
  }

  while (true) {
    wait_for_resolution_phase();  // Wait for all current recipe_fetch targets to finish
    weak_resolution_result const resolution{ resolve_weak_references() };
    if (resolution.resolved == 0 && resolution.fallbacks_started == 0) { break; }
  }
}

}  // namespace envy

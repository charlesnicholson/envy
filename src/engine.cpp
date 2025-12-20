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
#include <sstream>
#include <unordered_set>
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

bool has_dependency_path(recipe const *from, recipe const *to) {
  if (from == to) { return true; }

  // DFS to find if 'to' is reachable from 'from' via dependencies
  std::unordered_set<recipe const *> visited;
  std::vector<recipe const *> stack{ from };

  while (!stack.empty()) {
    recipe const *current{ stack.back() };
    stack.pop_back();

    if (visited.contains(current)) { continue; }
    visited.insert(current);

    for (auto const &[dep_identity, dep_info] : current->dependencies) {
      if (dep_info.recipe_ptr == to) { return true; }
      if (!visited.contains(dep_info.recipe_ptr)) { stack.push_back(dep_info.recipe_ptr); }
    }
  }

  return false;
}

void wire_dependency(recipe *parent, recipe *dep, recipe_phase needed_by) {
  if (!parent->dependencies.contains(dep->spec->identity)) {
    parent->dependencies[dep->spec->identity] = { dep, needed_by };
    ENVY_TRACE_DEPENDENCY_ADDED(parent->spec->identity, dep->spec->identity, needed_by);
  }

  if (std::ranges::find(parent->declared_dependencies, dep->spec->identity) ==
      parent->declared_dependencies.end()) {
    parent->declared_dependencies.push_back(dep->spec->identity);
  }
}

void resolve_identity_ref(recipe *r,
                          recipe::weak_reference *wr,
                          engine::weak_resolution_result &result,
                          std::vector<std::string> &ambiguity_messages,
                          engine &eng) {
  auto const matches{ eng.find_matches(wr->query) };

  if (matches.size() == 1) {
    recipe *dep{ matches[0] };

    if (has_dependency_path(dep, r)) {
      throw std::runtime_error("Weak dependency cycle detected: " + r->spec->identity +
                               " -> " + dep->spec->identity +
                               " (which already depends on " + r->spec->identity + ")");
    }

    wire_dependency(r, dep, wr->needed_by);
    wr->resolved = dep;
    if (wr->is_product) {
      auto const it{ r->product_dependencies.find(wr->query) };
      if (it != r->product_dependencies.end()) {
        it->second.provider = dep;
        if (it->second.constraint_identity.empty()) {
          it->second.constraint_identity = wr->constraint_identity;
        }
      }
    }
    ++result.resolved;
    return;
  }

  if (matches.size() > 1) {
    std::ostringstream oss;
    oss << "Reference '" << wr->query << "' in recipe '" << r->spec->identity
        << "' is ambiguous: ";
    for (size_t i = 0; i < matches.size(); ++i) {
      if (i) { oss << ", "; }
      oss << matches[i]->key.canonical();
    }
    ambiguity_messages.push_back(oss.str());
    return;
  }

  if (wr->fallback) {
    wr->fallback->parent = r->spec;

    recipe *dep{ eng.ensure_recipe(wr->fallback) };
    wire_dependency(r, dep, wr->needed_by);

    std::vector<std::string> child_chain{ eng.get_execution_ctx(r).ancestor_chain };
    child_chain.push_back(r->spec->identity);
    eng.start_recipe_thread(dep, recipe_phase::recipe_fetch, std::move(child_chain));

    wr->resolved = dep;
    if (wr->is_product) {
      auto const it{ r->product_dependencies.find(wr->query) };
      if (it != r->product_dependencies.end()) {
        it->second.provider = dep;
        if (it->second.constraint_identity.empty()) {
          it->second.constraint_identity = wr->constraint_identity;
        }
      }
    }
    ++result.fallbacks_started;
  }
}

void resolve_product_ref(recipe *r,
                         recipe::weak_reference *wr,
                         engine::weak_resolution_result &result,
                         std::unordered_map<std::string, recipe *> const &registry,
                         engine &eng) {
  auto set_product_provider = [&](recipe *provider) {
    auto const it{ r->product_dependencies.find(wr->query) };
    if (it != r->product_dependencies.end()) {
      it->second.provider = provider;
      if (!wr->constraint_identity.empty()) {
        it->second.constraint_identity = wr->constraint_identity;
      }
    }
  };

  auto const it{ registry.find(wr->query) };

  if (it != registry.end()) {
    recipe *dep{ it->second };

    if (!wr->constraint_identity.empty() &&
        dep->spec->identity != wr->constraint_identity) {
      throw std::runtime_error("Product '" + wr->query + "' in recipe '" +
                               r->spec->identity + "' must come from '" +
                               wr->constraint_identity + "', but provider is '" +
                               dep->spec->identity + "'");
    }

    if (has_dependency_path(dep, r)) {
      throw std::runtime_error("Weak dependency cycle detected: " + r->spec->identity +
                               " -> " + dep->spec->identity +
                               " (which already depends on " + r->spec->identity + ")");
    }

    wire_dependency(r, dep, wr->needed_by);
    wr->resolved = dep;
    set_product_provider(dep);
    ++result.resolved;
    return;
  }

  if (wr->fallback) {
    wr->fallback->parent = r->spec;

    recipe *dep{ eng.ensure_recipe(wr->fallback) };
    wire_dependency(r, dep, wr->needed_by);

    std::vector<std::string> child_chain{ eng.get_execution_ctx(r).ancestor_chain };
    child_chain.push_back(r->spec->identity);
    eng.start_recipe_thread(dep, recipe_phase::recipe_fetch, std::move(child_chain));

    wr->resolved = dep;
    set_product_provider(dep);
    ++result.fallbacks_started;
  }
}

bool recipe_provides_product_transitively_impl(
    recipe *r,
    std::string const &product_name,
    std::unordered_set<recipe const *> &visited) {
  if (!visited.insert(r).second) { return false; }

  ENVY_TRACE_EMIT((trace_events::product_transitive_check{
      .recipe = r->spec->identity,
      .product = product_name,
      .has_product_directly = r->products.contains(product_name),
      .dependency_count = r->dependencies.size() }));

  if (r->products.contains(product_name)) { return true; }

  for (auto const &[dep_id, dep_info] : r->dependencies) {
    ENVY_TRACE_EMIT(
        (trace_events::product_transitive_check_dep{ .recipe = r->spec->identity,
                                                     .product = product_name,
                                                     .checking_dependency = dep_id }));
    if (recipe_provides_product_transitively_impl(dep_info.recipe_ptr,
                                                  product_name,
                                                  visited)) {
      return true;
    }
  }

  return false;
}

}  // namespace

void engine_validate_dependency_cycle(std::string const &candidate_identity,
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
  fail_all_contexts();

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
    for (auto &[key, recipe] : recipes_) {
      for (auto &wr : recipe->weak_references) {
        if (!wr.resolved) { unresolved.emplace_back(recipe.get(), &wr); }
      }
    }
    return unresolved;
  };

  std::vector<std::string> ambiguity_messages;

  for (auto [r, wr] : collect_unresolved()) {
    if (wr->is_product) {
      resolve_product_ref(r, wr, result, product_registry_, *this);
    } else {
      resolve_identity_ref(r, wr, result, ambiguity_messages, *this);
    }
  }

  // If we spawned any fallback threads, wait for their recipe_fetch to complete
  // before checking for still-unresolved references
  if (result.fallbacks_started > 0) { wait_for_resolution_phase(); }

  // Final validation: any unresolved without fallback is an error
  std::vector<std::string> const missing_messages{ [&] {
    std::vector<std::string> msgs;
    for (auto [r, wr] : collect_unresolved()) {
      if (!wr->resolved && !wr->fallback) {
        if (wr->is_product) {
          msgs.push_back("Product '" + wr->query + "' in recipe '" + r->spec->identity +
                         "' was not found");
        } else {
          msgs.push_back("Reference '" + wr->query + "' in recipe '" + r->spec->identity +
                         "' was not found");
        }
      }
    }
    return msgs;
  }() };

  result.missing_without_fallback = missing_messages;

  if (!ambiguity_messages.empty()) {
    fail_all_contexts();
    std::ostringstream oss;
    for (size_t i{ 0 }; i < ambiguity_messages.size(); ++i) {
      if (i) { oss << "\n"; }
      oss << ambiguity_messages[i];
    }
    throw std::runtime_error(oss.str());
  }

  return result;
}

void recipe_execution_ctx::set_target_phase(recipe_phase target) {
  recipe_phase current_target{ target_phase.load() };
  while (current_target < target) {
    if (target_phase.compare_exchange_weak(current_target, target)) {
      std::lock_guard const lock(mutex);
      cv.notify_one();
      return;
    }
  }
}

void recipe_execution_ctx::start(recipe *r, engine *eng, std::vector<std::string> chain) {
  ancestor_chain = std::move(chain);
  worker = std::thread([r, eng] { eng->run_recipe_thread(r); });
}

recipe *engine::ensure_recipe(recipe_spec const *spec) {
  std::lock_guard const lock(mutex_);

  recipe_key const key(*spec);

  auto r{ std::unique_ptr<recipe>(new recipe{
      .key = key,
      .spec = spec,
      .exec_ctx = nullptr,
      .lua = nullptr,
      .lock = nullptr,
      .declared_dependencies = {},
      .owned_dependency_specs = {},
      .dependencies = {},
      .product_dependencies = {},
      .weak_references = {},
      .canonical_identity_hash = key.canonical(),
      .asset_path = std::filesystem::path{},
      .result_hash = {},
      .type = recipe_type::UNKNOWN,
      .cache_ptr = &cache_,
      .default_shell_ptr = &default_shell_,
      .tui_section = tui::section_create(),
  }) };

  auto const [it, inserted]{ recipes_.try_emplace(key, std::move(r)) };
  if (inserted) {
    execution_ctxs_[key] = std::make_unique<recipe_execution_ctx>();
    it->second->exec_ctx = execution_ctxs_[key].get();
    ENVY_TRACE_RECIPE_REGISTERED(spec->identity, key.canonical(), false);
  } else if (auto ctx_it = execution_ctxs_.find(key); ctx_it != execution_ctxs_.end()) {
    it->second->exec_ctx = ctx_it->second.get();
  }
  return it->second.get();
}

recipe *engine::find_exact(recipe_key const &key) const {
  std::lock_guard const lock(mutex_);
  auto const it{ recipes_.find(key) };
  return (it != recipes_.end()) ? it->second.get() : nullptr;
}

recipe *engine::find_product_provider(std::string const &product_name) const {
  std::lock_guard const lock(mutex_);
  auto const it{ product_registry_.find(product_name) };
  return it == product_registry_.end() ? nullptr : it->second;
}

std::vector<product_info> engine::collect_all_products() const {
  std::vector<product_info> infos;

  {
    std::lock_guard const lock(mutex_);

    for (auto const &[key, recipe] : recipes_) {
      for (auto const &[prod_name, prod_value] : recipe->products) {
        infos.push_back({
            .product_name = prod_name,
            .value = prod_value,
            .provider_canonical = recipe->key.canonical(),
            .type = recipe->type,
            .asset_path = recipe->asset_path,
        });
      }
    }
  }

  std::ranges::sort(infos, {}, &product_info::product_name);

  return infos;
}

std::vector<recipe *> engine::find_matches(std::string_view query) const {
  std::lock_guard const lock(mutex_);

  std::vector<recipe *> matches;
  for (auto const &[key, r] : recipes_) {
    if (key.matches(query)) { matches.push_back(r.get()); }
  }

  return matches;
}

recipe_execution_ctx &engine::get_execution_ctx(recipe *r) {
  return get_execution_ctx(r->key);
}

recipe_execution_ctx &engine::get_execution_ctx(recipe_key const &key) {
  std::lock_guard const lock(mutex_);
  auto const it{ execution_ctxs_.find(key) };
  if (it == execution_ctxs_.end()) {
    throw std::runtime_error("Recipe execution context not found: " + key.canonical());
  }
  return *it->second;
}

recipe_execution_ctx const &engine::get_execution_ctx(recipe_key const &key) const {
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

void engine::extend_dependencies_to_completion(recipe *r) {
  std::unordered_set<recipe_key> visited;
  extend_dependencies_recursive(r, visited);
}

std::filesystem::path const &engine::cache_root() const { return cache_.root(); }

void engine::extend_dependencies_recursive(recipe *r,
                                           std::unordered_set<recipe_key> &visited) {
  if (!visited.insert(r->key).second) { return; }  // Already visited (cycle detection)

  // Extend this recipe's target to completion
  auto &ctx{ get_execution_ctx(r) };
  recipe_phase const old_target{ ctx.target_phase.load() };

  if (old_target < recipe_phase::completion) {
    ENVY_TRACE_TARGET_EXTENDED(r->spec->identity, old_target, recipe_phase::completion);
  }

  ctx.set_target_phase(recipe_phase::completion);

  // Recursively extend all dependencies
  for (auto const &[dep_identity, dep_info] : r->dependencies) {
    extend_dependencies_recursive(dep_info.recipe_ptr, visited);
  }
}

#ifdef ENVY_UNIT_TEST
recipe_phase engine::get_recipe_target_phase(recipe_key const &key) const {
  auto const &ctx{ get_execution_ctx(key) };
  return ctx.target_phase.load();
}
#endif

void engine::wait_for_resolution_phase() {
  std::unique_lock lock(mutex_);
  cv_.wait(lock, [this] { return pending_recipe_fetches_ == 0; });
}

void engine::notify_phase_complete() { notify_all_global_locked(); }

void engine::on_recipe_fetch_start() {
  int const new_value{ pending_recipe_fetches_.fetch_add(1) + 1 };
  ENVY_TRACE_RECIPE_FETCH_COUNTER_INC("engine", new_value);
}

void engine::on_recipe_fetch_complete(std::string const &recipe_identity) {
  int const new_value{ pending_recipe_fetches_.fetch_sub(1) - 1 };
  ENVY_TRACE_RECIPE_FETCH_COUNTER_DEC(recipe_identity, new_value, true);
  if (new_value == 0) { notify_all_global_locked(); }
}

void engine::process_fetch_dependencies(recipe *r,
                                        std::vector<std::string> const &ancestor_chain) {
  // Process fetch dependencies - added to dependencies map with needed_by=recipe_fetch
  // Existing phase loop wait logic handles blocking automatically
  for (auto *fetch_dep_spec : r->spec->source_dependencies) {
    fetch_dep_spec->parent = r->spec;  // Set parent pointer for custom fetch lookup

    engine_validate_dependency_cycle(fetch_dep_spec->identity,
                                     ancestor_chain,
                                     r->spec->identity,
                                     "Fetch dependency");

    if (fetch_dep_spec->is_weak_reference()) {  // Defer resolution to weak pass
      recipe::weak_reference wr;
      wr.query = fetch_dep_spec->identity;
      wr.needed_by = recipe_phase::recipe_fetch;
      wr.fallback = fetch_dep_spec->weak;
      r->weak_references.push_back(std::move(wr));
      continue;
    }

    recipe *fetch_dep{ ensure_recipe(fetch_dep_spec) };

    // Add to dependencies map - phase loop will handle blocking at recipe_fetch
    r->dependencies[fetch_dep_spec->identity] = { fetch_dep, recipe_phase::recipe_fetch };
    ENVY_TRACE_DEPENDENCY_ADDED(r->spec->identity,
                                fetch_dep_spec->identity,
                                recipe_phase::recipe_fetch);

    // Build child ancestor chain (local to this thread path)
    std::vector<std::string> child_chain{ ancestor_chain };
    child_chain.push_back(r->spec->identity);

    start_recipe_thread(fetch_dep, recipe_phase::completion, std::move(child_chain));
  }
}

void engine::run_recipe_thread(recipe *r) {
  auto &ctx{ get_execution_ctx(r) };

  try {
    if (!r->spec->source_dependencies.empty()) {
      process_fetch_dependencies(r, ctx.ancestor_chain);
    }

    while (ctx.current_phase < recipe_phase::completion) {
      if (ctx.failed) { break; }
      recipe_phase const target{ ctx.target_phase };
      recipe_phase const current{ ctx.current_phase };

      if (current >= target) {  // Check if we've reached target
        std::unique_lock lock(ctx.mutex);
        ctx.cv.wait(lock,
                    [&ctx, current] { return ctx.target_phase > current || ctx.failed; });

        if (ctx.target_phase == current || ctx.failed) { break; }
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

      ctx.current_phase = next;  // Phase is now active
      phase_dispatch_table[static_cast<int>(next)](r, *this);

      if (next == recipe_phase::recipe_fetch) {
        ctx.recipe_fetch_completed = true;
        on_recipe_fetch_complete(r->spec->identity);
      }
      notify_phase_complete();
    }
    ENVY_TRACE_THREAD_COMPLETE(r->spec->identity, ctx.current_phase);
  } catch (...) {
    std::string error_msg;
    try {
      throw;  // rethrow to inspect
    } catch (std::exception const &e) { error_msg = e.what(); } catch (...) {
      error_msg = "unknown exception";
    }

    {
      std::lock_guard lock(ctx.mutex);
      ctx.error_message = std::move(error_msg);
    }

    ctx.failed = true;
    if (!ctx.recipe_fetch_completed) { on_recipe_fetch_complete(r->spec->identity); }
    notify_all_global_locked();
  }
}

recipe_result_map_t engine::run_full(std::vector<recipe_spec const *> const &roots) {
  try {
    resolve_graph(roots);
  } catch (...) {
    fail_all_contexts();

    for (auto &[key, ctx] : execution_ctxs_) {  // Best-effort join to avoid leaks
      if (ctx->worker.joinable()) { ctx->worker.join(); }
    }

    throw;
  }

  {
    std::lock_guard lock(mutex_);               // Protect iteration over execution_ctxs_
    for (auto &[key, ctx] : execution_ctxs_) {  // Launch all recipes running to completion
      ctx->set_target_phase(recipe_phase::completion);
    }
  }

  tui::debug("engine: joining %zu recipe threads", execution_ctxs_.size());
  for (auto &[key, ctx] : execution_ctxs_) {  // Wait for all recipes to complete
    if (ctx->worker.joinable()) { ctx->worker.join(); }
  }
  tui::debug("engine: all recipe threads joined");

  {
    std::lock_guard lock(mutex_);
    for (auto const &[key, ctx] : execution_ctxs_) {  // Check for failures
      if (ctx->failed) {
        std::lock_guard ctx_lock(ctx->mutex);
        std::string const msg{ ctx->error_message.empty()
                                   ? "Recipe failed: " + key.canonical()
                                   : ctx->error_message };
        throw std::runtime_error(msg);
      }
    }
  }

  recipe_result_map_t results;
  {
    std::lock_guard lock(mutex_);
    for (auto const &[key, r] : recipes_) {
      auto const &ctx{ execution_ctxs_.at(key) };
      recipe_type const result_type{ ctx->failed ? recipe_type::UNKNOWN : r->type };
      results[r->key.canonical()] = { result_type, r->result_hash, r->asset_path };
    }
  }
  return results;
}

void engine::fail_all_contexts() {
  std::lock_guard const lock(mutex_);
  for (auto &[_, ctx] : execution_ctxs_) {
    ctx->failed = true;
    ctx->target_phase = recipe_phase::completion;
    ctx->current_phase = recipe_phase::completion;
    ctx->cv.notify_all();
  }
  cv_.notify_all();
}

void engine::update_product_registry() {
  std::unordered_map<std::string, std::vector<recipe *>> providers_by_product;

  {
    std::lock_guard const lock(mutex_);
    for (auto &[key, recipe] : recipes_) {
      auto const ctx_it{ execution_ctxs_.find(key) };
      if (ctx_it == execution_ctxs_.end()) { continue; }
      if (ctx_it->second->current_phase.load() < recipe_phase::recipe_fetch) { continue; }

      for (auto const &[product_name, _] : recipe->products) {
        // Skip already-registered providers (added in prior iterations)
        if (product_registry_.contains(product_name)) { continue; }
        providers_by_product[product_name].push_back(recipe.get());
      }
    }
  }

  std::vector<std::string> collisions;

  for (auto const &[product_name, providers] : providers_by_product) {
    if (providers.size() == 1) {
      // Collision if an existing provider was already registered
      if (product_registry_.contains(product_name)) {
        collisions.push_back("Product '" + product_name +
                             "' provided by multiple recipes: " +
                             product_registry_.at(product_name)->spec->identity + ", " +
                             providers.front()->spec->identity);
      } else {
        product_registry_[product_name] = providers.front();
      }
    } else if (!providers.empty()) {
      std::ostringstream oss;
      oss << "Product '" << product_name << "' provided by multiple recipes: ";
      for (size_t i{ 0 }; i < providers.size(); ++i) {
        if (i) { oss << ", "; }
        oss << providers[i]->spec->identity;
      }
      collisions.push_back(oss.str());
    }
  }

  if (!collisions.empty()) {
    std::ostringstream oss;
    for (size_t i{ 0 }; i < collisions.size(); ++i) {
      if (i) { oss << "\n"; }
      oss << collisions[i];
    }
    throw std::runtime_error(oss.str());
  }
}

void engine::validate_product_fallbacks() {
  std::vector<std::pair<recipe *, recipe::weak_reference *>> to_validate;

  {
    std::lock_guard const lock(mutex_);
    for (auto &[_, recipe] : recipes_) {
      for (auto &wr : recipe->weak_references) {
        if (wr.is_product && wr.fallback && wr.resolved) {
          to_validate.emplace_back(recipe.get(), &wr);
        }
      }
    }
  }

  std::vector<std::string> errors;

  for (auto const &[r, wr] : to_validate) {
    std::unordered_set<recipe const *> visited;
    if (!recipe_provides_product_transitively(wr->resolved, wr->query)) {
      errors.push_back("Fallback for product '" + wr->query + "' in recipe '" +
                       r->spec->identity + "' resolved to '" +
                       wr->resolved->spec->identity +
                       "', which does not provide product transitively");
    }
  }

  if (!errors.empty()) {
    std::ostringstream oss;
    for (size_t i{ 0 }; i < errors.size(); ++i) {
      if (i) { oss << "\n"; }
      oss << errors[i];
    }
    throw std::runtime_error(oss.str());
  }
}

bool engine::recipe_provides_product_transitively(recipe *r,
                                                  std::string const &product_name) const {
  std::unordered_set<recipe const *> visited;
  return recipe_provides_product_transitively_impl(r, product_name, visited);
}

void engine::resolve_graph(std::vector<recipe_spec const *> const &roots) {
  for (auto const *spec : roots) {
    recipe *const r{ ensure_recipe(spec) };
    tui::debug("engine: resolve_graph start thread for %s", spec->identity.c_str());
    start_recipe_thread(r, recipe_phase::recipe_fetch);
  }

  auto const count_unresolved{ [this]() {
    std::lock_guard const lock(mutex_);
    size_t count{ 0 };
    for (auto &[_, recipe] : recipes_) {
      for (auto &wr : recipe->weak_references) {
        if (!wr.resolved) { ++count; }
      }
    }
    return count;
  } };

  size_t iteration{ 0 };
  while (true) {
    ++iteration;
    wait_for_resolution_phase();  // Wait for all current recipe_fetch targets to finish
    update_product_registry();
    weak_resolution_result const resolution{ resolve_weak_references() };

    if (resolution.resolved == 0 && resolution.fallbacks_started == 0) {
      size_t const unresolved{ count_unresolved() };
      if (!resolution.missing_without_fallback.empty()) {
        fail_all_contexts();
        std::ostringstream oss;
        for (size_t i{ 0 }; i < resolution.missing_without_fallback.size(); ++i) {
          if (i) { oss << "\n"; }
          oss << resolution.missing_without_fallback[i];
        }
        oss << "\nWeak dependency resolution made no progress at iteration " << iteration
            << " with " << unresolved << " unresolved references";
        throw std::runtime_error(oss.str());
      }
      if (unresolved > 0) {
        fail_all_contexts();
        throw std::runtime_error(
            "Weak dependency resolution made no progress at iteration " +
            std::to_string(iteration) + " with " + std::to_string(unresolved) +
            " unresolved references");
      }
      break;
    }
  }

  validate_product_fallbacks();

  {  // Cache resolved weak dependency keys for thread-safe hash computation
    std::lock_guard const lock(mutex_);
    for (auto &[_, recipe] : recipes_) {
      recipe->resolved_weak_dependency_keys.clear();
      for (auto const &wr : recipe->weak_references) {
        if (wr.resolved) {
          recipe->resolved_weak_dependency_keys.push_back(wr.resolved->key.canonical());
        }
      }
      std::sort(recipe->resolved_weak_dependency_keys.begin(),
                recipe->resolved_weak_dependency_keys.end());
    }
  }
}

}  // namespace envy

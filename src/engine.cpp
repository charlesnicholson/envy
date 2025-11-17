#include "engine.h"

#include "manifest.h"
#include "recipe.h"
#include "recipe_key.h"
#include "tui.h"

#include <vector>

namespace envy {

engine::engine(cache &cache, default_shell_cfg_t default_shell)
    : cache_(cache), default_shell_(default_shell) {}

engine::~engine() {
  for (auto &[key, ctx] : execution_ctxs_) {
    if (ctx->worker.joinable()) { ctx->worker.join(); }
  }
}

recipe *engine::ensure_recipe(recipe_spec const &spec) {
  std::lock_guard lock(mutex_);

  recipe_key key(spec);

  auto [it, inserted] =
      recipes_.try_emplace(key,
                           std::make_unique<recipe>(recipe{
                               .key = key,
                               .spec = spec,
                               .lua_state = nullptr,
                               .lock = nullptr,
                               .declared_dependencies = {},
                               .dependencies = {},
                               .canonical_identity_hash = key.canonical(),
                               .asset_path = {},
                               .result_hash = {},
                               .cache_ptr = &cache_,
                               .default_shell_ptr = &default_shell_,
                           }));

  if (inserted) { execution_ctxs_[key] = std::make_unique<recipe_execution_ctx>(); }
  return it->second.get();
}

void engine::register_alias(std::string const &alias, recipe_key const &key) {
  std::lock_guard lock(mutex_);

  if (recipes_.find(key) == recipes_.end()) {
    throw std::runtime_error("Cannot register alias '" + alias +
                             "': recipe not found: " + key.canonical());
  }

  auto [it, inserted] = aliases_.try_emplace(alias, key);
  if (!inserted) { throw std::runtime_error("Alias already registered: " + alias); }
}

recipe *engine::find_exact(recipe_key const &key) const {
  std::lock_guard lock(mutex_);
  auto it = recipes_.find(key);
  return (it != recipes_.end()) ? it->second.get() : nullptr;
}

std::vector<recipe *> engine::find_matches(std::string_view query) const {
  std::lock_guard lock(mutex_);

  auto alias_it = aliases_.find(std::string(query));
  if (alias_it != aliases_.end()) {
    recipe *r = find_exact(alias_it->second);
    return r ? std::vector<recipe *>{ r } : std::vector<recipe *>{};
  }

  std::vector<recipe *> matches;
  for (auto const &[key, r] : recipes_) {
    if (key.matches(query)) { matches.push_back(r.get()); }
  }

  return matches;
}

void engine::ensure_recipe_at_phase(recipe_key const &key, int target) {
  auto ctx_it = execution_ctxs_.find(key);
  if (ctx_it == execution_ctxs_.end()) {
    throw std::runtime_error("Recipe not found: " + key.canonical());
  }
  auto &ctx = *ctx_it->second;

  // Extend target if needed (atomic compare-exchange)
  int current_target = ctx.target_phase.load();
  while (current_target < target) {
    if (ctx.target_phase.compare_exchange_weak(current_target, target)) {
      ctx.cv.notify_one();
      break;
    }
  }

  // Wait for recipe to reach target
  std::unique_lock lock(mutex_);
  cv_.wait(lock, [&ctx, target] {
    return ctx.current_phase.load() >= target || ctx.failed.load();
  });

  if (ctx.failed.load()) { throw std::runtime_error("Recipe failed: " + key.canonical()); }
}

void engine::wait_for_resolution_phase() {
  std::unique_lock lock(mutex_);
  cv_.wait(lock, [this] { return pending_recipe_fetches_.load() == 0; });
}

void engine::notify_phase_complete(recipe_key const &key, int phase) {
  (void)key;
  (void)phase;
  cv_.notify_all();
}

void engine::on_recipe_fetch_start() { pending_recipe_fetches_.fetch_add(1); }

void engine::on_recipe_fetch_complete() {
  if (pending_recipe_fetches_.fetch_sub(1) == 1) { cv_.notify_all(); }
}

void engine::run_recipe_thread(recipe *r) {
  auto &ctx = *execution_ctxs_.at(r->key);

  while (true) {
    int target = ctx.target_phase.load();
    int current = ctx.current_phase.load();

    // Check if we've reached target
    if (current >= target) {
      if (target == 7) break;  // Final phase, exit

      // Wait for target extension
      std::unique_lock lock(ctx.mutex);
      ctx.cv.wait(lock, [&ctx, current] {
        return ctx.target_phase.load() > current || ctx.failed.load();
      });

      if (ctx.target_phase.load() == current || ctx.failed.load()) break;
    }

    // Run next phase (TODO: implement phase dispatch in Phase 3)
    int next = current + 1;

    // For now, just mark as complete (Phase 3 will add actual phase execution)
    ctx.current_phase.store(next);
    notify_phase_complete(r->key, next);
  }
}

recipe_result_map_t engine::run_full(std::vector<recipe_spec> const &roots) {
  resolve_graph(roots);

  for (auto const &[key, r] : recipes_) { ensure_recipe_at_phase(key, 7); }

  for (auto &[key, ctx] : execution_ctxs_) {
    if (ctx->worker.joinable()) { ctx->worker.join(); }
  }

  recipe_result_map_t results;
  for (auto const &[key, r] : recipes_) {
    results[r->key.canonical()] = { r->result_hash, r->asset_path };
  }
  return results;
}

void engine::resolve_graph(std::vector<recipe_spec> const &roots) {
  for (auto const &spec : roots) {
    recipe *r = ensure_recipe(spec);
    if (spec.alias) { register_alias(*spec.alias, r->key); }

    auto &ctx = *execution_ctxs_.at(r->key);
    ctx.target_phase = 0;  // Just recipe_fetch
    ctx.worker = std::thread([r, this] { run_recipe_thread(r); });
  }

  wait_for_resolution_phase();
}

}  // namespace envy

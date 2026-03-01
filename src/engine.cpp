#include "engine.h"

#include "manifest.h"
#include "package_depot.h"
#include "phases/phase_build.h"
#include "phases/phase_check.h"
#include "phases/phase_completion.h"
#include "phases/phase_fetch.h"
#include "phases/phase_install.h"
#include "phases/phase_spec_fetch.h"
#include "phases/phase_stage.h"
#include "pkg.h"
#include "pkg_key.h"
#include "pkg_phase.h"
#include "platform.h"
#include "tui.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <sstream>
#include <unordered_set>
#include <vector>

namespace envy {

namespace {

using phase_func_t = void (*)(pkg *, engine &);

constexpr std::array<phase_func_t, pkg_phase_count> phase_dispatch_table{
  run_spec_fetch_phase,  // pkg_phase::spec_fetch
  run_check_phase,       // pkg_phase::pkg_check
  run_fetch_phase,       // pkg_phase::pkg_fetch
  run_stage_phase,       // pkg_phase::pkg_stage
  run_build_phase,       // pkg_phase::pkg_build
  run_install_phase,     // pkg_phase::pkg_install
  run_completion_phase,  // pkg_phase::completion
};

bool has_dependency_path(pkg const *from, pkg const *to) {
  if (from == to) { return true; }

  // DFS to find if 'to' is reachable from 'from' via dependencies
  std::unordered_set<pkg const *> visited;
  std::vector<pkg const *> stack{ from };

  while (!stack.empty()) {
    pkg const *current{ stack.back() };
    stack.pop_back();

    if (visited.contains(current)) { continue; }
    visited.insert(current);

    for (auto const &[dep_identity, dep_info] : current->dependencies) {
      if (dep_info.p == to) { return true; }
      if (!visited.contains(dep_info.p)) { stack.push_back(dep_info.p); }
    }
  }

  return false;
}

void wire_dependency(pkg *parent, pkg *dep, pkg_phase needed_by) {
  if (!parent->dependencies.contains(dep->cfg->identity)) {
    parent->dependencies[dep->cfg->identity] = { dep, needed_by };
    ENVY_TRACE_DEPENDENCY_ADDED(parent->cfg->identity, dep->cfg->identity, needed_by);
  }

  if (std::ranges::find(parent->declared_dependencies, dep->cfg->identity) ==
      parent->declared_dependencies.end()) {
    parent->declared_dependencies.push_back(dep->cfg->identity);
  }
}

void resolve_identity_ref(pkg *p,
                          pkg::weak_reference *wr,
                          engine::weak_resolution_result &result,
                          std::vector<std::string> &ambiguity_messages,
                          engine &eng) {
  auto const matches{ eng.find_matches(wr->query) };

  if (matches.size() == 1) {
    pkg *dep{ matches[0] };

    if (has_dependency_path(dep, p)) {
      throw std::runtime_error("Weak dependency cycle detected: " + p->cfg->identity +
                               " -> " + dep->cfg->identity +
                               " (which already depends on " + p->cfg->identity + ")");
    }

    wire_dependency(p, dep, wr->needed_by);
    wr->resolved = dep;
    if (wr->is_product) {
      auto const it{ p->product_dependencies.find(wr->query) };
      if (it != p->product_dependencies.end()) {
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
    oss << "Reference '" << wr->query << "' in spec '" << p->cfg->identity
        << "' is ambiguous: ";
    for (size_t i = 0; i < matches.size(); ++i) {
      if (i) { oss << ", "; }
      oss << matches[i]->key.canonical();
    }
    ambiguity_messages.push_back(oss.str());
    return;
  }

  if (wr->fallback) {
    wr->fallback->parent = p->cfg;

    pkg *dep{ eng.ensure_pkg(wr->fallback) };
    wire_dependency(p, dep, wr->needed_by);

    std::vector<std::string> child_chain{ eng.get_execution_ctx(p).ancestor_chain };
    child_chain.push_back(p->cfg->identity);
    eng.start_pkg_thread(dep, pkg_phase::spec_fetch, std::move(child_chain));

    wr->resolved = dep;
    if (wr->is_product) {
      auto const it{ p->product_dependencies.find(wr->query) };
      if (it != p->product_dependencies.end()) {
        it->second.provider = dep;
        if (it->second.constraint_identity.empty()) {
          it->second.constraint_identity = wr->constraint_identity;
        }
      }
    }
    ++result.fallbacks_started;
  }
}

void resolve_product_ref(pkg *p,
                         pkg::weak_reference *wr,
                         engine::weak_resolution_result &result,
                         std::unordered_map<std::string, pkg *> const &registry,
                         engine &eng) {
  auto set_product_provider = [&](pkg *provider) {
    auto const it{ p->product_dependencies.find(wr->query) };
    if (it != p->product_dependencies.end()) {
      it->second.provider = provider;
      if (!wr->constraint_identity.empty()) {
        it->second.constraint_identity = wr->constraint_identity;
      }
    }
  };

  auto const it{ registry.find(wr->query) };

  if (it != registry.end()) {
    pkg *dep{ it->second };

    if (!wr->constraint_identity.empty() &&
        dep->cfg->identity != wr->constraint_identity) {
      throw std::runtime_error("Product '" + wr->query + "' in spec '" + p->cfg->identity +
                               "' must come from '" + wr->constraint_identity +
                               "', but provider is '" + dep->cfg->identity + "'");
    }

    if (has_dependency_path(dep, p)) {
      throw std::runtime_error("Weak dependency cycle detected: " + p->cfg->identity +
                               " -> " + dep->cfg->identity +
                               " (which already depends on " + p->cfg->identity + ")");
    }

    wire_dependency(p, dep, wr->needed_by);
    wr->resolved = dep;
    set_product_provider(dep);
    ++result.resolved;
    return;
  }

  if (wr->fallback) {
    wr->fallback->parent = p->cfg;

    pkg *dep{ eng.ensure_pkg(wr->fallback) };
    wire_dependency(p, dep, wr->needed_by);

    std::vector<std::string> child_chain{ eng.get_execution_ctx(p).ancestor_chain };
    child_chain.push_back(p->cfg->identity);
    eng.start_pkg_thread(dep, pkg_phase::spec_fetch, std::move(child_chain));

    wr->resolved = dep;
    set_product_provider(dep);
    ++result.fallbacks_started;
  }
}

bool pkg_provides_product_transitively_impl(pkg *p,
                                            std::string const &product_name,
                                            std::unordered_set<pkg const *> &visited) {
  if (!visited.insert(p).second) { return false; }

  ENVY_TRACE_EMIT((trace_events::product_transitive_check{
      .spec = p->cfg->identity,
      .product = product_name,
      .has_product_directly = p->products.contains(product_name),
      .dependency_count = p->dependencies.size() }));

  if (p->products.contains(product_name)) { return true; }

  for (auto const &[dep_id, dep_info] : p->dependencies) {
    ENVY_TRACE_EMIT(
        (trace_events::product_transitive_check_dep{ .spec = p->cfg->identity,
                                                     .product = product_name,
                                                     .checking_dependency = dep_id }));
    if (pkg_provides_product_transitively_impl(dep_info.p, product_name, visited)) {
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

engine::engine(cache &cache, manifest const *manifest)
    : cache_(cache),
      default_shell_(manifest ? manifest->get_default_shell() : std::nullopt),
      manifest_(manifest) {}

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
    std::vector<std::pair<pkg *, pkg::weak_reference *>> unresolved;
    std::lock_guard const lock(mutex_);
    for (auto &[key, package] : packages_) {
      for (auto &wr : package->weak_references) {
        if (!wr.resolved) { unresolved.emplace_back(package.get(), &wr); }
      }
    }
    return unresolved;
  };

  std::vector<std::string> ambiguity_messages;

  for (auto [p, wr] : collect_unresolved()) {
    if (wr->is_product) {
      resolve_product_ref(p, wr, result, product_registry_, *this);
    } else {
      resolve_identity_ref(p, wr, result, ambiguity_messages, *this);
    }
  }

  // If we spawned any fallback threads, wait for their spec_fetch to complete
  // before checking for still-unresolved references
  if (result.fallbacks_started > 0) { wait_for_resolution_phase(); }

  // Final validation: any unresolved without fallback is an error
  std::vector<std::string> const missing_messages{ [&] {
    std::vector<std::string> msgs;
    for (auto [p, wr] : collect_unresolved()) {
      if (!wr->resolved && !wr->fallback) {
        if (wr->is_product) {
          msgs.push_back("Product '" + wr->query + "' in spec '" + p->cfg->identity +
                         "' was not found");
        } else {
          msgs.push_back("Reference '" + wr->query + "' in spec '" + p->cfg->identity +
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

void pkg_execution_ctx::set_target_phase(pkg_phase target) {
  pkg_phase current_target{ target_phase.load() };
  while (current_target < target) {
    if (target_phase.compare_exchange_weak(current_target, target)) {
      std::lock_guard const lock(mutex);
      cv.notify_one();
      return;
    }
  }
}

void pkg_execution_ctx::start(pkg *p, engine *eng, std::vector<std::string> chain) {
  ancestor_chain = std::move(chain);
  worker = std::thread([p, eng] { eng->run_pkg_thread(p); });
}

pkg *engine::ensure_pkg(pkg_cfg const *cfg) {
  std::lock_guard const lock(mutex_);

  pkg_key const key(*cfg);

  auto p{ std::unique_ptr<pkg>(new pkg{ .key = key,
                                        .cfg = cfg,
                                        .cache_ptr = &cache_,
                                        .default_shell_ptr = &default_shell_,
                                        .tui_section = tui::section_create(),
                                        .exec_ctx = nullptr,
                                        .lua = nullptr,
                                        .lock = nullptr,
                                        .canonical_identity_hash = key.canonical(),
                                        .pkg_path = std::filesystem::path{},
                                        .result_hash = {},
                                        .type = pkg_type::UNKNOWN,
                                        .declared_dependencies = {},
                                        .owned_dependency_cfgs = {},
                                        .dependencies = {},
                                        .product_dependencies = {},
                                        .weak_references = {} }) };

  auto const [it, inserted]{ packages_.try_emplace(key, std::move(p)) };
  if (inserted) {
    execution_ctxs_[key] = std::make_unique<pkg_execution_ctx>();
    it->second->exec_ctx = execution_ctxs_[key].get();
    ENVY_TRACE_RECIPE_REGISTERED(cfg->identity, key.canonical(), false);
  } else if (auto ctx_it = execution_ctxs_.find(key); ctx_it != execution_ctxs_.end()) {
    it->second->exec_ctx = ctx_it->second.get();
  }
  return it->second.get();
}

pkg *engine::find_exact(pkg_key const &key) const {
  std::lock_guard const lock(mutex_);
  auto const it{ packages_.find(key) };
  return (it != packages_.end()) ? it->second.get() : nullptr;
}

pkg *engine::find_product_provider(std::string const &product_name) const {
  std::lock_guard const lock(mutex_);
  auto const it{ product_registry_.find(product_name) };
  return it == product_registry_.end() ? nullptr : it->second;
}

std::vector<product_info> engine::collect_all_products() const {
  std::vector<product_info> infos;

  {
    std::lock_guard const lock(mutex_);

    for (auto const &[key, package] : packages_) {
      for (auto const &[prod_name, prod_entry] : package->products) {
        infos.push_back({
            .product_name = prod_name,
            .value = prod_entry.value,
            .provider_canonical = package->key.canonical(),
            .type = package->type,
            .pkg_path = package->pkg_path,
            .script = prod_entry.script,
        });
      }
    }
  }

  std::ranges::sort(infos, {}, &product_info::product_name);

  return infos;
}

std::vector<pkg *> engine::find_matches(std::string_view query) const {
  std::lock_guard const lock(mutex_);

  std::vector<pkg *> matches;
  for (auto const &[key, p] : packages_) {
    if (key.matches(query)) { matches.push_back(p.get()); }
  }

  return matches;
}

pkg_execution_ctx &engine::get_execution_ctx(pkg *p) { return get_execution_ctx(p->key); }

pkg_execution_ctx &engine::get_execution_ctx(pkg_key const &key) {
  std::lock_guard const lock(mutex_);
  auto const it{ execution_ctxs_.find(key) };
  if (it == execution_ctxs_.end()) {
    throw std::runtime_error("Package execution context not found: " + key.canonical());
  }
  return *it->second;
}

pkg_execution_ctx const &engine::get_execution_ctx(pkg_key const &key) const {
  std::lock_guard const lock(mutex_);
  auto const it{ execution_ctxs_.find(key) };
  if (it == execution_ctxs_.end()) {
    throw std::runtime_error("Package execution context not found: " + key.canonical());
  }
  return *it->second;
}

void engine::start_pkg_thread(pkg *p,
                              pkg_phase initial_target,
                              std::vector<std::string> ancestor_chain) {
  auto &ctx{ get_execution_ctx(p) };

  bool expected{ false };
  if (ctx.started.compare_exchange_strong(expected, true)) {  // set phase then start
    if (initial_target >= pkg_phase::spec_fetch) { on_spec_fetch_start(); }
    ctx.set_target_phase(initial_target);
    ENVY_TRACE_THREAD_START(p->cfg->identity, initial_target);
    ctx.start(p, this, std::move(ancestor_chain));
  } else {  // already started, extend target if needed
    ctx.set_target_phase(initial_target);
  }
}

void engine::ensure_pkg_at_phase(pkg_key const &key, pkg_phase const target) {
  auto &ctx{ get_execution_ctx(key) };

  ctx.set_target_phase(target);  // Extend target if needed

  // Wait for package to reach target
  std::unique_lock lock(mutex_);
  cv_.wait(lock, [&ctx, target] { return ctx.current_phase >= target || ctx.failed; });

  if (ctx.failed) {
    std::lock_guard ctx_lock(ctx.mutex);
    std::string const msg{ ctx.error_message.empty() ? "Package failed: " + key.canonical()
                                                     : ctx.error_message };
    throw std::runtime_error(msg);
  }
}

void engine::extend_dependencies_to_completion(pkg *p) {
  std::unordered_set<pkg_key> visited;
  extend_dependencies_recursive(p, visited);
}

std::filesystem::path const &engine::cache_root() const { return cache_.root(); }

manifest const *engine::get_manifest() const { return manifest_; }

package_depot_index const *engine::depot_index() const {
  if (!manifest_ || manifest_->meta.package_depots.empty()) { return nullptr; }

  std::call_once(depot_init_flag_, [this] {
    namespace fs = std::filesystem;
    auto const depot_tmp{ fs::temp_directory_path() /
                          ("envy-depot-" + std::to_string(platform::get_process_id())) };
    try {
      std::error_code ec;
      fs::create_directories(depot_tmp, ec);
      depot_index_ = package_depot_index::build(manifest_->meta.package_depots, depot_tmp);
    } catch (std::exception const &e) {
      tui::warn("failed to build depot index: %s", e.what());
    }
    std::error_code ec;
    fs::remove_all(depot_tmp, ec);
  });

  return depot_index_ ? &*depot_index_ : nullptr;
}

bundle *engine::register_bundle(std::string const &identity,
                                std::unordered_map<std::string, std::string> specs,
                                std::filesystem::path cache_path) {
  std::lock_guard const lock{ mutex_ };

  // Check if already registered
  auto it{ bundle_registry_.find(identity) };
  if (it != bundle_registry_.end()) { return it->second.get(); }

  // Create new bundle and register
  auto b{ std::make_unique<bundle>() };
  b->identity = identity;
  b->specs = std::move(specs);
  b->cache_path = std::move(cache_path);

  auto [insert_it, inserted]{ bundle_registry_.emplace(identity, std::move(b)) };
  return insert_it->second.get();
}

bundle *engine::find_bundle(std::string const &identity) const {
  std::lock_guard const lock{ mutex_ };
  auto it{ bundle_registry_.find(identity) };
  return it != bundle_registry_.end() ? it->second.get() : nullptr;
}

void engine::extend_dependencies_recursive(pkg *p, std::unordered_set<pkg_key> &visited) {
  if (!visited.insert(p->key).second) { return; }  // Already visited (cycle detection)

  // Extend this package's target to completion
  auto &ctx{ get_execution_ctx(p) };
  pkg_phase const old_target{ ctx.target_phase.load() };

  if (old_target < pkg_phase::completion) {
    ENVY_TRACE_TARGET_EXTENDED(p->cfg->identity, old_target, pkg_phase::completion);
  }

  ctx.set_target_phase(pkg_phase::completion);

  // Recursively extend all dependencies
  for (auto const &[dep_identity, dep_info] : p->dependencies) {
    extend_dependencies_recursive(dep_info.p, visited);
  }
}

#ifdef ENVY_UNIT_TEST
pkg_phase engine::get_pkg_target_phase(pkg_key const &key) const {
  auto const &ctx{ get_execution_ctx(key) };
  return ctx.target_phase.load();
}
#endif

void engine::wait_for_resolution_phase() {
  std::unique_lock lock(mutex_);
  cv_.wait(lock, [this] { return pending_spec_fetches_ == 0; });
}

void engine::notify_phase_complete() { notify_all_global_locked(); }

void engine::on_spec_fetch_start() {
  int const new_value{ pending_spec_fetches_.fetch_add(1) + 1 };
  ENVY_TRACE_SPEC_FETCH_COUNTER_INC("engine", new_value);
}

void engine::on_spec_fetch_complete(std::string const &pkg_identity) {
  int const new_value{ pending_spec_fetches_.fetch_sub(1) - 1 };
  ENVY_TRACE_SPEC_FETCH_COUNTER_DEC(pkg_identity, new_value, true);
  if (new_value == 0) { notify_all_global_locked(); }
}

void engine::process_fetch_dependencies(pkg *p,
                                        std::vector<std::string> const &ancestor_chain) {
  // Process fetch dependencies - added to dependencies map with needed_by=spec_fetch
  // Existing phase loop wait logic handles blocking automatically
  for (auto *fetch_dep_cfg : p->cfg->source_dependencies) {
    // Set parent pointer for custom fetch lookup, but only for spec-declared deps.
    // Manifest-declared bundles (parent already null, identity == bundle_identity)
    // should keep null parent - their fetch function is in the manifest, not a spec.
    bool const is_manifest_bundle{ !fetch_dep_cfg->parent &&
                                   fetch_dep_cfg->bundle_identity.has_value() &&
                                   fetch_dep_cfg->identity ==
                                       *fetch_dep_cfg->bundle_identity };
    if (!is_manifest_bundle) {
      fetch_dep_cfg->parent = p->cfg;  // Set parent pointer for custom fetch lookup
    }

    engine_validate_dependency_cycle(fetch_dep_cfg->identity,
                                     ancestor_chain,
                                     p->cfg->identity,
                                     "Fetch dependency");

    if (fetch_dep_cfg->is_weak_reference()) {  // Defer resolution to weak pass
      pkg::weak_reference wr;
      wr.query = fetch_dep_cfg->identity;
      wr.needed_by = pkg_phase::spec_fetch;
      wr.fallback = fetch_dep_cfg->weak;
      p->weak_references.push_back(std::move(wr));
      continue;
    }

    pkg *fetch_dep{ ensure_pkg(fetch_dep_cfg) };

    // Add to dependencies map - phase loop will handle blocking at spec_fetch
    p->dependencies[fetch_dep_cfg->identity] = { fetch_dep, pkg_phase::spec_fetch };
    ENVY_TRACE_DEPENDENCY_ADDED(p->cfg->identity,
                                fetch_dep_cfg->identity,
                                pkg_phase::spec_fetch);

    // Build child ancestor chain (local to this thread path)
    std::vector<std::string> child_chain{ ancestor_chain };
    child_chain.push_back(p->cfg->identity);

    start_pkg_thread(fetch_dep, pkg_phase::completion, std::move(child_chain));
  }
}

void engine::run_pkg_thread(pkg *p) {
  auto &ctx{ get_execution_ctx(p) };

  try {
    if (!p->cfg->source_dependencies.empty()) {
      process_fetch_dependencies(p, ctx.ancestor_chain);
    }

    while (ctx.current_phase < pkg_phase::completion) {
      if (ctx.failed) { break; }
      pkg_phase const target{ ctx.target_phase };
      pkg_phase const current{ ctx.current_phase };

      if (current >= target) {  // Check if we've reached target
        std::unique_lock lock(ctx.mutex);
        ctx.cv.wait(lock,
                    [&ctx, current] { return ctx.target_phase > current || ctx.failed; });

        if (ctx.target_phase == current || ctx.failed) { break; }
        ENVY_TRACE_TARGET_EXTENDED(p->cfg->identity, current, ctx.target_phase.load());
      }

      pkg_phase const next{ static_cast<pkg_phase>(static_cast<int>(current) + 1) };
      if (static_cast<int>(next) < 0 || static_cast<int>(next) >= pkg_phase_count) {
        throw std::runtime_error("Invalid phase: " +
                                 std::to_string(static_cast<int>(next)));
      }

      // Wait for dependencies that are needed by this phase
      // If dependency has needed_by=build, we must wait for it before entering build phase
      for (auto const &[dep_identity, dep_info] : p->dependencies) {
        if (next >= dep_info.needed_by) {
          // Dependency is needed by this or earlier phase, ensure it's fully complete
          ENVY_TRACE_PHASE_BLOCKED(p->cfg->identity,
                                   next,
                                   dep_identity,
                                   pkg_phase::completion);
          ensure_pkg_at_phase(dep_info.p->key, pkg_phase::completion);
          ENVY_TRACE_PHASE_UNBLOCKED(p->cfg->identity, next, dep_identity);
        }
      }

      ctx.current_phase = next;  // Phase is now active
      phase_dispatch_table[static_cast<int>(next)](p, *this);

      if (next == pkg_phase::spec_fetch) {
        ctx.spec_fetch_completed = true;
        on_spec_fetch_complete(p->cfg->identity);

        // BUNDLE_ONLY packages stop after spec_fetch - no lua state to execute
        if (p->type == pkg_type::BUNDLE_ONLY) {
          ctx.current_phase = pkg_phase::completion;
          notify_phase_complete();  // Wake waiters before exiting
          break;
        }
      }
      notify_phase_complete();
    }
    ENVY_TRACE_THREAD_COMPLETE(p->cfg->identity, ctx.current_phase);
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
    if (!ctx.spec_fetch_completed) { on_spec_fetch_complete(p->cfg->identity); }
    notify_all_global_locked();
  }
}

pkg_result_map_t engine::run_full(std::vector<pkg_cfg const *> const &roots) {
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
    std::lock_guard lock(mutex_);
    for (auto &[key, ctx] :
         execution_ctxs_) {  // Launch all packages running to completion
      ctx->set_target_phase(pkg_phase::completion);
    }
  }

  tui::debug("engine: joining %zu package threads", execution_ctxs_.size());
  for (auto &[key, ctx] : execution_ctxs_) {  // Wait for all packages to complete
    if (ctx->worker.joinable()) { ctx->worker.join(); }
  }
  tui::debug("engine: all package threads joined");

  {
    std::lock_guard lock(mutex_);
    for (auto const &[key, ctx] : execution_ctxs_) {  // Check for failures
      if (ctx->failed) {
        std::lock_guard ctx_lock(ctx->mutex);
        std::string const msg{ ctx->error_message.empty()
                                   ? "Package failed: " + key.canonical()
                                   : ctx->error_message };
        throw std::runtime_error(msg);
      }
    }
  }

  auto const results{ [&] {
    pkg_result_map_t r;
    std::lock_guard lock(mutex_);
    for (auto const &[key, package] : packages_) {
      auto const &ctx{ execution_ctxs_.at(key) };
      pkg_type const result_type{ ctx->failed ? pkg_type::UNKNOWN : package->type };
      r[package->key.canonical()] = { result_type,
                                      package->result_hash,
                                      package->pkg_path };
    }
    return r;
  }() };

  return results;
}

void engine::fail_all_contexts() {
  std::lock_guard const lock(mutex_);
  for (auto &[_, ctx] : execution_ctxs_) {
    ctx->failed = true;
    ctx->target_phase = pkg_phase::completion;
    ctx->current_phase = pkg_phase::completion;
    ctx->cv.notify_all();
  }
  cv_.notify_all();
}

void engine::update_product_registry() {
  std::unordered_map<std::string, std::vector<pkg *>> providers_by_product;

  {
    std::lock_guard const lock(mutex_);
    for (auto &[key, package] : packages_) {
      auto const ctx_it{ execution_ctxs_.find(key) };
      if (ctx_it == execution_ctxs_.end()) { continue; }
      if (ctx_it->second->current_phase.load() < pkg_phase::spec_fetch) { continue; }

      for (auto const &[product_name, _] : package->products) {
        // Skip already-registered providers (added in prior iterations)
        if (product_registry_.contains(product_name)) { continue; }
        providers_by_product[product_name].push_back(package.get());
      }
    }
  }

  std::vector<std::string> collisions;

  for (auto const &[product_name, providers] : providers_by_product) {
    if (providers.size() == 1) {
      // Collision if an existing provider was already registered
      if (product_registry_.contains(product_name)) {
        collisions.push_back("Product '" + product_name +
                             "' provided by multiple specs: " +
                             product_registry_.at(product_name)->cfg->identity + ", " +
                             providers.front()->cfg->identity);
      } else {
        product_registry_[product_name] = providers.front();
      }
    } else if (!providers.empty()) {
      std::ostringstream oss;
      oss << "Product '" << product_name << "' provided by multiple specs: ";
      for (size_t i{ 0 }; i < providers.size(); ++i) {
        if (i) { oss << ", "; }
        oss << providers[i]->cfg->identity;
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
  std::vector<std::pair<pkg *, pkg::weak_reference *>> to_validate;

  {
    std::lock_guard const lock(mutex_);
    for (auto &[_, package] : packages_) {
      for (auto &wr : package->weak_references) {
        if (wr.is_product && wr.fallback && wr.resolved) {
          to_validate.emplace_back(package.get(), &wr);
        }
      }
    }
  }

  std::vector<std::string> errors;

  for (auto const &[p, wr] : to_validate) {
    std::unordered_set<pkg const *> visited;
    if (!pkg_provides_product_transitively(wr->resolved, wr->query)) {
      errors.push_back("Fallback for product '" + wr->query + "' in spec '" +
                       p->cfg->identity + "' resolved to '" + wr->resolved->cfg->identity +
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

bool engine::pkg_provides_product_transitively(pkg *p,
                                               std::string const &product_name) const {
  std::unordered_set<pkg const *> visited;
  return pkg_provides_product_transitively_impl(p, product_name, visited);
}

void engine::resolve_graph(std::vector<pkg_cfg const *> const &roots) {
  for (auto const *cfg : roots) {
    pkg *const p{ ensure_pkg(cfg) };
    tui::debug("engine: resolve_graph start thread for %s", cfg->identity.c_str());
    start_pkg_thread(p, pkg_phase::spec_fetch);
  }

  auto const count_unresolved{ [this]() {
    std::lock_guard const lock(mutex_);
    size_t count{ 0 };
    for (auto &[_, package] : packages_) {
      for (auto &wr : package->weak_references) {
        if (!wr.resolved) { ++count; }
      }
    }
    return count;
  } };

  auto const collect_failed_packages{ [this]() {
    std::vector<std::string> errors;
    std::lock_guard const lock(mutex_);
    for (auto const &[key, ctx] : execution_ctxs_) {
      if (ctx->failed) {
        std::lock_guard const ctx_lock(ctx->mutex);
        errors.push_back(ctx->error_message.empty() ? "Package failed: " + key.canonical()
                                                    : ctx->error_message);
      }
    }
    return errors;
  } };

  size_t iteration{ 0 };
  while (true) {
    ++iteration;
    wait_for_resolution_phase();

    if (auto const errors{ collect_failed_packages() }; !errors.empty()) {
      fail_all_contexts();
      std::ostringstream oss;
      for (size_t i{ 0 }; i < errors.size(); ++i) {
        if (i) { oss << "\n"; }
        oss << errors[i];
      }
      throw std::runtime_error(oss.str());
    }

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
        oss << "\nDependency resolution made no progress at iteration " << iteration
            << " with " << unresolved << " unresolved references";
        throw std::runtime_error(oss.str());
      }
      if (unresolved > 0) {
        fail_all_contexts();
        throw std::runtime_error("Dependency resolution made no progress at iteration " +
                                 std::to_string(iteration) + " with " +
                                 std::to_string(unresolved) + " unresolved references");
      }
      break;
    }
  }

  validate_product_fallbacks();

  {  // Cache resolved weak dependency keys for thread-safe hash computation
    std::lock_guard const lock(mutex_);
    for (auto &[_, package] : packages_) {
      package->resolved_weak_dependency_keys.clear();
      for (auto const &wr : package->weak_references) {
        if (wr.resolved) {
          package->resolved_weak_dependency_keys.push_back(wr.resolved->key.canonical());
        }
      }
      std::sort(package->resolved_weak_dependency_keys.begin(),
                package->resolved_weak_dependency_keys.end());
    }
  }
}

}  // namespace envy

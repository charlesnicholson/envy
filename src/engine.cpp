#include "engine.h"

#include "manifest.h"
#include "package_depot.h"
#include "phases/phase_build.h"
#include "phases/phase_check.h"
#include "phases/phase_completion.h"
#include "phases/phase_export.h"
#include "phases/phase_fetch.h"
#include "phases/phase_import.h"
#include "phases/phase_install.h"
#include "phases/phase_setup.h"
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

// Watermark after which a dependency's artifacts and host-side SETUP state are
// available: setup completed (= export may begin). Dependencies are waited on
// to this point (not completion) so that export can overlap with dependents'
// builds — the edge ratchets the dependency through export without waiting for
// it. Setup must complete before dependents proceed: user-managed packages do
// all their work in SETUP pairs.
constexpr int kDependencySatisfiedWatermark{ static_cast<int>(pkg_phase::pkg_setup) + 1 };
constexpr int kDependencyRunThroughWatermark{ static_cast<int>(pkg_phase::pkg_export) +
                                              1 };
static_assert(kDependencySatisfiedWatermark == static_cast<int>(pkg_phase::pkg_export) &&
                  kDependencySatisfiedWatermark < static_cast<int>(pkg_phase::completion),
              "pkg_setup must be followed by pkg_export before completion");

using phase_func_t = void (*)(pkg *, engine &);

constexpr std::array<phase_func_t, pkg_phase_count> phase_dispatch_table{
  run_spec_fetch_phase,  // pkg_phase::spec_fetch
  run_check_phase,       // pkg_phase::pkg_check
  run_import_phase,      // pkg_phase::pkg_import
  run_fetch_phase,       // pkg_phase::pkg_fetch
  run_stage_phase,       // pkg_phase::pkg_stage
  run_build_phase,       // pkg_phase::pkg_build
  run_install_phase,     // pkg_phase::pkg_install
  run_setup_phase,       // pkg_phase::pkg_setup
  run_export_phase,      // pkg_phase::pkg_export
  run_completion_phase,  // pkg_phase::completion
};

// Trace mapping: watermark w = "first w steps completed"; the corresponding
// "current" phase in legacy trace terms is the last completed step, w - 1.
constexpr pkg_phase phase_from_watermark(int w) { return static_cast<pkg_phase>(w - 1); }

constexpr bool is_setup_pair_key(std::string_view key) {
  return key.find("#setup:") != std::string_view::npos;
}

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

    std::lock_guard const deps_lock(current->deps_mutex);
    for (auto const &[dep_identity, dep_info] : current->dependencies) {
      if (dep_info.p == to) { return true; }
      if (!visited.contains(dep_info.p)) { stack.push_back(dep_info.p); }
    }
  }

  return false;
}

void wire_dependency(pkg *parent, pkg *dep, pkg_phase needed_by) {
  std::lock_guard const deps_lock(parent->deps_mutex);

  if (!parent->dependencies.contains(dep->cfg->identity)) {
    parent->dependencies[dep->cfg->identity] = { dep, needed_by };
    ENVY_TRACE_DEPENDENCY_ADDED(parent->cfg->identity, dep->cfg->identity, needed_by);
  }

  if (std::ranges::find(parent->declared_dependencies, dep->cfg->identity) ==
      parent->declared_dependencies.end()) {
    parent->declared_dependencies.push_back(dep->cfg->identity);
  }
}

// Merge a weak reference's SETUP selection into the package it resolved to
// (union with all other referrers). Weak resolution wires to an existing
// package without an ensure_pkg call, so the selection is merged here instead.
// Runs during graph resolution, strictly before any setup phase executes, so
// the consumed guard never fires in practice — kept for symmetry with
// ensure_pkg and to catch a pathologically late resolution. Locks only `dep`.
void merge_setup_selection(pkg *dep, std::vector<std::string> const &names) {
  if (names.empty()) { return; }
  std::lock_guard const deps_lock(dep->deps_mutex);
  size_t const before{ dep->setup_selected.size() };
  dep->setup_selected.insert(names.begin(), names.end());
  if (dep->setup_selection_consumed && dep->setup_selected.size() != before) {
    throw std::runtime_error(
        "SETUP selection for " + dep->cfg->identity +
        " arrived after its setup phase ran; a weak dependency selected a pair "
        "that resolved too late");
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
    merge_setup_selection(dep, wr->setup);
    {
      std::lock_guard const deps_lock(p->deps_mutex);
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
    merge_setup_selection(dep, wr->setup);

    std::vector<std::string> child_chain{ p->ancestor_chain };
    child_chain.push_back(p->cfg->identity);
    eng.start_pkg_thread(dep, pkg_phase::spec_fetch, std::move(child_chain));

    {
      std::lock_guard const deps_lock(p->deps_mutex);
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
    std::lock_guard const deps_lock(p->deps_mutex);
    wr->resolved = provider;
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
    merge_setup_selection(dep, wr->setup);
    set_product_provider(dep);
    ++result.resolved;
    return;
  }

  if (wr->fallback) {
    wr->fallback->parent = p->cfg;

    pkg *dep{ eng.ensure_pkg(wr->fallback) };
    wire_dependency(p, dep, wr->needed_by);
    merge_setup_selection(dep, wr->setup);

    std::vector<std::string> child_chain{ p->ancestor_chain };
    child_chain.push_back(p->cfg->identity);
    eng.start_pkg_thread(dep, pkg_phase::spec_fetch, std::move(child_chain));

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

  auto const deps{ [&] {
    std::lock_guard const deps_lock(p->deps_mutex);
    std::vector<std::pair<std::string, pkg *>> snapshot;
    snapshot.reserve(p->dependencies.size());
    for (auto const &[dep_id, dep_info] : p->dependencies) {
      snapshot.emplace_back(dep_id, dep_info.p);
    }
    return snapshot;
  }() };

  for (auto const &[dep_id, dep] : deps) {
    ENVY_TRACE_EMIT(
        (trace_events::product_transitive_check_dep{ .spec = p->cfg->identity,
                                                     .product = product_name,
                                                     .checking_dependency = dep_id }));
    if (pkg_provides_product_transitively_impl(dep, product_name, visited)) {
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
      manifest_(manifest),
      core_(make_trace_observer()) {}

engine::~engine() = default;  // core_ (declared last) fails + joins workers first

std::string engine::trace_display(std::string const &key) const {
  if (is_setup_pair_key(key)) { return key; }
  std::lock_guard const lock(mutex_);
  auto const it{ packages_.find(pkg_key{ key }) };
  return it != packages_.end() ? it->second->cfg->identity : key;
}

task_engine::observer engine::make_trace_observer() {
  task_engine::observer obs;

  obs.thread_start = [this](std::string const &key, int target) {
    if (!tui::g_trace_enabled) { return; }
    ENVY_TRACE_THREAD_START(
        trace_display(key),
        is_setup_pair_key(key) ? pkg_phase::pkg_setup : phase_from_watermark(target));
  };
  obs.thread_complete = [this](std::string const &key, int completed) {
    if (!tui::g_trace_enabled) { return; }
    ENVY_TRACE_THREAD_COMPLETE(
        trace_display(key),
        is_setup_pair_key(key) ? pkg_phase::completion : phase_from_watermark(completed));
  };
  obs.blocked =
      [this](std::string const &key, int step, std::string const &dep, int watermark) {
        if (!tui::g_trace_enabled) { return; }
        bool const pair{ is_setup_pair_key(key) };
        ENVY_TRACE_PHASE_BLOCKED(
            trace_display(key),
            pair ? pkg_phase::pkg_setup : static_cast<pkg_phase>(step),
            trace_display(dep),
            pair ? pkg_phase::completion : static_cast<pkg_phase>(watermark));
      };
  obs.unblocked = [this](std::string const &key, int step, std::string const &dep) {
    if (!tui::g_trace_enabled) { return; }
    ENVY_TRACE_PHASE_UNBLOCKED(
        trace_display(key),
        is_setup_pair_key(key) ? pkg_phase::pkg_setup : static_cast<pkg_phase>(step),
        trace_display(dep));
  };
  obs.target_extended = [this](std::string const &key, int old_done, int new_target) {
    if (!tui::g_trace_enabled) { return; }
    ENVY_TRACE_TARGET_EXTENDED(trace_display(key),
                               phase_from_watermark(old_done),
                               phase_from_watermark(new_target));
  };

  return obs;
}

task_engine::task_config engine::make_pkg_task_config(pkg *p) {
  task_engine::task_config cfg;
  cfg.key = p->key.canonical();
  cfg.step_count = pkg_phase_count;

  cfg.on_start = [this, p] {
    // Fetch/source dependencies must be wired before step 0's edge query so
    // spec loading blocks on the bundles it needs.
    if (!p->cfg->source_dependencies.empty()) { process_fetch_dependencies(p); }
  };

  cfg.edges = [p](int step) {
    // Snapshot under deps_mutex — the resolution loop may wire weak deps into
    // this map concurrently. Re-waiting satisfied edges on later steps is cheap.
    std::vector<task_engine::edge> edges;
    std::lock_guard const deps_lock(p->deps_mutex);
    for (auto const &[dep_identity, dep_info] : p->dependencies) {
      if (step >= static_cast<int>(dep_info.needed_by)) {
        edges.push_back({ dep_info.p->key.canonical(),
                          kDependencySatisfiedWatermark,
                          kDependencyRunThroughWatermark });
      }
    }
    return edges;
  };

  cfg.step = [this, p](int step) {
    p->current_phase.store(static_cast<pkg_phase>(step));
    phase_dispatch_table[step](p, *this);

    if (static_cast<pkg_phase>(step) == pkg_phase::spec_fetch) {
      p->spec_fetch_completed = true;
      on_spec_fetch_complete(p->cfg->identity);

      // BUNDLE_ONLY packages stop after spec_fetch - no lua state to execute
      if (p->type == pkg_type::BUNDLE_ONLY) { return true; }
    }
    return false;
  };

  cfg.on_failed = [this, p] {
    if (!p->spec_fetch_completed) { on_spec_fetch_complete(p->cfg->identity); }
  };

  return cfg;
}

pkg *engine::ensure_pkg(pkg_cfg const *cfg) {
  pkg_key const key(*cfg);
  pkg *result{ nullptr };
  bool inserted{ false };

  {
    std::lock_guard const lock(mutex_);

    auto p{ std::unique_ptr<pkg>(new pkg{ .key = key,
                                          .cfg = cfg,
                                          .cache_ptr = &cache_,
                                          .default_shell_ptr = &default_shell_,
                                          .tui_section = tui::section_create(),
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

    auto const [it, was_inserted]{ packages_.try_emplace(key, std::move(p)) };
    inserted = was_inserted;
    result = it->second.get();

    if (inserted) { ENVY_TRACE_RECIPE_REGISTERED(cfg->identity, key.canonical(), false); }

    if (cfg->setup.has_value() && !cfg->setup->empty()) {
      // Merge explicit SETUP selection across referrers (union). Selection is
      // explicit-only: a referrer that omits `setup` requests nothing.
      std::lock_guard const deps_lock(result->deps_mutex);
      size_t const before{ result->setup_selected.size() };
      result->setup_selected.insert(cfg->setup->begin(), cfg->setup->end());
      if (result->setup_selection_consumed && result->setup_selected.size() != before) {
        throw std::runtime_error(
            "SETUP selection for " + cfg->identity +
            " arrived after its setup phase ran; select pairs from entries that "
            "resolve before the package executes");
      }
    }

    // Task creation must be atomic with the packages_ insert (still under
    // mutex_; engine mutex -> core mutex ordering, never reversed): a second
    // thread that loses the try_emplace returns immediately and may start or
    // wait on the task before this thread would otherwise have created it.
    if (inserted && !core_.ensure_task(make_pkg_task_config(result))) {
      // A fresh package key must never collide with an existing task (e.g. a
      // pathological identity matching a pair-task key). Fail loudly now
      // instead of hanging later on a task with someone else's config.
      throw std::runtime_error("Package task key collides with existing task: " +
                               key.canonical());
    }
  }

  return result;
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
        auto plats{ util_platform_intersect(prod_entry.platforms,
                                            package->resolved_platforms) };
        if (plats.empty() && !prod_entry.platforms.empty() &&
            !package->resolved_platforms.empty()) {
          plats.emplace_back(kPlatformNone);
        }
        infos.push_back({
            .product_name = prod_name,
            .value = prod_entry.value,
            .provider_canonical = package->key.canonical(),
            .type = package->type,
            .pkg_path = package->pkg_path,
            .script = prod_entry.script,
            .platforms = std::move(plats),
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

void engine::start_pkg_thread(pkg *p,
                              pkg_phase run_through,
                              std::vector<std::string> ancestor_chain) {
  // before_spawn runs exactly once, before the worker exists: the ancestor
  // chain must be visible to the worker, and the spec-fetch counter must rise
  // before the worker can decrement it.
  core_.start_task(p->key.canonical(),
                   watermark_through(run_through),
                   [this, p, &ancestor_chain] {
                     p->ancestor_chain = std::move(ancestor_chain);
                     on_spec_fetch_start();
                   });
}

void engine::extend_to_completion(pkg_key const &key) {
  core_.extend_to_done(key.canonical());
}

void engine::wait_for_completion(pkg_key const &key) {
  std::string const canonical{ key.canonical() };
  core_.wait_at(canonical, core_.step_count(canonical));
}

void engine::run_setup_pairs_for(pkg *parent, std::vector<std::string> const &pair_names) {
  // Pair name → task key; the selection closure guarantees every DEPENDS
  // target of a selected pair is itself selected.
  std::unordered_map<std::string, std::string> key_of;
  for (auto const &name : pair_names) {
    key_of.emplace(name, parent->key.canonical() + "#setup:" + name);
  }

  for (auto const &name : pair_names) {
    std::string const &key{ key_of.at(name) };

    std::vector<task_engine::edge> sibling_edges;
    for (auto const &dep : parent->setup_pairs.at(name).depends) {
      sibling_edges.push_back({ key_of.at(dep), 1 });
    }

    tui::section_handle const section{ tui::section_create() };

    task_engine::task_config cfg;
    cfg.key = key;
    cfg.step_count = 1;
    cfg.edges = [edges = std::move(sibling_edges)](int) { return edges; };
    cfg.step = [this, parent, name, section, key](int) {
      run_setup_pair(parent, *this, name, section, key);
      if (section && tui::section_has_content(section)) {
        tui::section_set_content(
            section,
            tui::section_frame{ .label = "[" + key + "]",
                                .content = tui::static_text_data{ .text = "done" } });
        tui::section_set_complete(section);
      }
      return false;
    };

    if (!core_.ensure_task(std::move(cfg))) {
      throw std::runtime_error("SETUP pair task key collides with existing task: " + key);
    }
  }

  // Edges are baked into each config, so start order is irrelevant.
  for (auto const &name : pair_names) { core_.start_task(key_of.at(name), 1); }

  // Wait for every pair and aggregate failures so one bad pair doesn't mask
  // the others; unrelated in-flight pairs run to completion.
  std::string errors;
  for (auto const &name : pair_names) {
    try {
      core_.wait_at(key_of.at(name), 1);
    } catch (std::exception const &e) {
      errors += errors.empty() ? "" : "\n";
      errors += e.what();
    }
  }
  if (!errors.empty()) { throw std::runtime_error(errors); }
}

void engine::extend_dependencies_to_completion(pkg *p) {
  std::unordered_set<pkg_key> visited;
  extend_dependencies_recursive(p, visited);
}

std::filesystem::path const &engine::cache_root() const { return cache_.root(); }

manifest const *engine::get_manifest() const { return manifest_; }

void engine::set_depot_index(package_depot_index idx) {
  depot_index_ = std::move(idx);
  depot_pre_set_ = true;
}

void engine::set_ignore_depot(bool ignore) { depot_ignored_ = ignore; }

void engine::set_export_config(export_phase_config cfg) {
  export_config_ = std::move(cfg);
}

export_phase_config const *engine::export_config() const {
  return export_config_ ? &*export_config_ : nullptr;
}

void engine::record_export_result(pkg_key const &key, std::string output_line) {
  std::lock_guard const lock{ mutex_ };
  export_results_[key.canonical()] = std::move(output_line);
}

std::string const *engine::get_export_result(pkg_key const &key) const {
  std::lock_guard const lock{ mutex_ };
  auto it{ export_results_.find(key.canonical()) };
  return it != export_results_.end() ? &it->second : nullptr;
}

package_depot_index const *engine::depot_index() const {
  // Pre-set index (set before thread creation, safe to read without synchronization)
  if (depot_pre_set_) { return &*depot_index_; }

  if (depot_ignored_) { return nullptr; }

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
  std::string const canonical{ p->key.canonical() };
  int const old_target{ core_.target(canonical) };

  if (old_target < core_.step_count(canonical)) {
    ENVY_TRACE_TARGET_EXTENDED(p->cfg->identity,
                               phase_from_watermark(old_target),
                               pkg_phase::completion);
  }

  core_.extend_to_done(canonical);

  // Recursively extend all dependencies (snapshot: no nested pkg locks)
  auto const deps{ [&] {
    std::lock_guard const deps_lock(p->deps_mutex);
    std::vector<pkg *> snapshot;
    snapshot.reserve(p->dependencies.size());
    for (auto const &[_, dep_info] : p->dependencies) { snapshot.push_back(dep_info.p); }
    return snapshot;
  }() };
  for (auto *dep : deps) { extend_dependencies_recursive(dep, visited); }
}

#ifdef ENVY_UNIT_TEST
pkg_phase engine::get_pkg_target_phase(pkg_key const &key) const {
  return phase_from_watermark(core_.target(key.canonical()));
}
#endif

void engine::wait_for_resolution_phase() {
  core_.wait_global([this] { return pending_spec_fetches_ == 0; });
}

void engine::on_spec_fetch_start() {
  int const new_value{ pending_spec_fetches_.fetch_add(1) + 1 };
  ENVY_TRACE_SPEC_FETCH_COUNTER_INC("engine", new_value);
}

void engine::on_spec_fetch_complete(std::string const &pkg_identity) {
  int const new_value{ pending_spec_fetches_.fetch_sub(1) - 1 };
  ENVY_TRACE_SPEC_FETCH_COUNTER_DEC(pkg_identity, new_value, true);
  if (new_value == 0) { core_.notify_global(); }
}

void engine::process_fetch_dependencies(pkg *p) {
  // Process fetch dependencies - added to dependencies map with needed_by=spec_fetch
  // The per-step edge query handles blocking automatically
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
                                     p->ancestor_chain,
                                     p->cfg->identity,
                                     "Fetch dependency");

    if (fetch_dep_cfg->is_weak_reference()) {  // Defer resolution to weak pass
      pkg::weak_reference wr;
      wr.query = fetch_dep_cfg->identity;
      wr.needed_by = pkg_phase::spec_fetch;
      wr.fallback = fetch_dep_cfg->weak;
      std::lock_guard const deps_lock(p->deps_mutex);
      p->weak_references.push_back(std::move(wr));
      continue;
    }

    pkg *fetch_dep{ ensure_pkg(fetch_dep_cfg) };

    // Add to dependencies map - the edge query will block spec_fetch on it
    {
      std::lock_guard const deps_lock(p->deps_mutex);
      p->dependencies[fetch_dep_cfg->identity] = { fetch_dep, pkg_phase::spec_fetch };
    }
    ENVY_TRACE_DEPENDENCY_ADDED(p->cfg->identity,
                                fetch_dep_cfg->identity,
                                pkg_phase::spec_fetch);

    // Build child ancestor chain (local to this thread path)
    std::vector<std::string> child_chain{ p->ancestor_chain };
    child_chain.push_back(p->cfg->identity);

    start_pkg_thread(fetch_dep, pkg_phase::completion, std::move(child_chain));
  }
}

std::vector<pkg_cfg const *> engine_filter_host_platform(
    std::vector<pkg_cfg const *> const &cfgs) {
  std::vector<pkg_cfg const *> result;
  result.reserve(cfgs.size());
  for (auto const *cfg : cfgs) {
    if (util_platform_matches(cfg->platforms,
                              platform::os_name(),
                              platform::arch_name())) {
      result.push_back(cfg);
    }
  }
  return result;
}

std::vector<pkg_cfg const *> engine_resolve_targets(
    std::vector<pkg_cfg *> const &packages,
    std::vector<std::string> const &queries,
    std::string const &cmd_name) {
  if (queries.empty()) { return { packages.begin(), packages.end() }; }

  std::vector<pkg_cfg const *> targets;
  for (auto const &query : queries) {
    bool found{ false };
    for (auto const *pkg : packages) {
      if (pkg_key const key{ *pkg }; key.matches(query)) {
        if (!util_platform_matches(pkg->platforms,
                                   platform::os_name(),
                                   platform::arch_name())) {
          throw std::runtime_error(cmd_name + ": '" + query +
                                   "' is not available on this platform");
        }
        targets.push_back(pkg);
        found = true;
        break;
      }
    }
    if (!found) {
      throw std::runtime_error(cmd_name + ": query '" + query + "' not found in manifest");
    }
  }

  return targets;
}

pkg_result_map_t engine::run_full(std::vector<pkg_cfg const *> const &roots) {
  auto const filtered{ engine_filter_host_platform(roots) };

  try {
    resolve_graph(filtered);
  } catch (...) {
    core_.fail_all();
    core_.join_all();  // Best-effort join to avoid leaks
    throw;
  }

  core_.extend_all_to_done();  // Launch all tasks running to completion

  tui::debug("engine: joining package threads");
  core_.join_all();  // Tolerates pair tasks spawned while joining
  tui::debug("engine: all package threads joined");

  if (auto const failures{ core_.collect_failures() }; !failures.empty()) {
    auto const &[key, msg]{ failures.front() };
    throw std::runtime_error(msg.empty() ? "Package failed: " + key : msg);
  }

  auto const results{ [&] {
    pkg_result_map_t r;
    std::lock_guard lock(mutex_);
    for (auto const &[key, package] : packages_) {
      pkg_type const result_type{ core_.failed(key.canonical()) ? pkg_type::UNKNOWN
                                                                : package->type };
      r[package->key.canonical()] = { result_type,
                                      package->result_hash,
                                      package->pkg_path };
    }
    return r;
  }() };

  return results;
}

void engine::update_product_registry() {
  std::unordered_map<std::string, std::vector<pkg *>> providers_by_product;

  // mutex_ guards product_registry_ for the whole function - readers
  // (find_product_provider) may run concurrently on worker threads.
  std::lock_guard const lock(mutex_);

  for (auto &[key, package] : packages_) {
    if (!package->spec_fetch_completed.load()) { continue; }

    for (auto const &[product_name, _] : package->products) {
      // Skip already-registered providers (added in prior iterations)
      if (product_registry_.contains(product_name)) { continue; }
      providers_by_product[product_name].push_back(package.get());
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

void engine::validate_setup_selections() {
  // A weak reference may select SETUP pairs on whatever package it resolves to.
  // A selection only makes sense if the resolved package runs a setup phase and
  // declares the named pair. Validate once, after the graph is fully resolved
  // (all spec_fetches complete, so type/setup_pairs are populated), so the
  // author sees a precise error before any fetch/build work begins.
  std::vector<std::pair<pkg *, pkg::weak_reference *>> to_validate;
  {
    std::lock_guard const lock(mutex_);
    for (auto &[_, package] : packages_) {
      for (auto &wr : package->weak_references) {
        if (!wr.setup.empty() && wr.resolved) {
          to_validate.emplace_back(package.get(), &wr);
        }
      }
    }
  }

  std::vector<std::string> errors;

  for (auto const &[requester, wr] : to_validate) {
    pkg *const target{ wr->resolved };
    std::string const context{ "spec '" + requester->cfg->identity +
                               "' weak-depends on '" + wr->query + "' (resolved to '" +
                               target->cfg->identity + "')" };

    // (a) target must actually run a setup phase (see run_setup_phase).
    if (target->type != pkg_type::CACHE_MANAGED &&
        target->type != pkg_type::USER_MANAGED) {
      for (auto const &name : wr->setup) {
        errors.push_back(context + " selects SETUP pair '" + name +
                         "', but that package runs no setup phase");
      }
      continue;
    }

    // (b) every selected pair must be declared by the resolved package.
    for (auto const &name : wr->setup) {
      if (target->setup_pairs.contains(name)) { continue; }
      std::string detail{ target->setup_pairs.empty() ? "it declares no SETUP pairs"
                                                       : "declared pairs:" };
      for (auto const &[pair_name, _] : target->setup_pairs) { detail += " " + pair_name; }
      errors.push_back(context + " selects SETUP pair '" + name + "', but " + detail);
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

engine::weak_resolution_result engine::resolve_weak_references() {
  weak_resolution_result result{};

  auto collect_unresolved = [this]() {
    std::vector<std::pair<pkg *, pkg::weak_reference *>> unresolved;
    std::lock_guard const lock(mutex_);
    for (auto &[key, package] : packages_) {
      std::lock_guard const deps_lock(package->deps_mutex);
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
    core_.fail_all();
    std::ostringstream oss;
    for (size_t i{ 0 }; i < ambiguity_messages.size(); ++i) {
      if (i) { oss << "\n"; }
      oss << ambiguity_messages[i];
    }
    throw std::runtime_error(oss.str());
  }

  return result;
}

void engine::resolve_graph(std::vector<pkg_cfg const *> const &roots) {
  // Register all roots before starting any thread so every manifest cfg's
  // SETUP selection is merged before a dependency thread can race ensure_pkg.
  std::vector<pkg *> root_pkgs;
  root_pkgs.reserve(roots.size());
  for (auto const *cfg : roots) { root_pkgs.push_back(ensure_pkg(cfg)); }

  for (size_t i{ 0 }; i < root_pkgs.size(); ++i) {
    tui::debug("engine: resolve_graph start thread for %s", roots[i]->identity.c_str());
    start_pkg_thread(root_pkgs[i], pkg_phase::spec_fetch);
  }

  auto const count_unresolved{ [this]() {
    std::lock_guard const lock(mutex_);
    size_t count{ 0 };
    for (auto &[_, package] : packages_) {
      std::lock_guard const deps_lock(package->deps_mutex);
      for (auto &wr : package->weak_references) {
        if (!wr.resolved) { ++count; }
      }
    }
    return count;
  } };

  auto const collect_failed{ [this]() {
    std::vector<std::string> errors;
    for (auto const &[key, msg] : core_.collect_failures()) {
      errors.push_back(msg.empty() ? "Package failed: " + key : msg);
    }
    return errors;
  } };

  size_t iteration{ 0 };
  while (true) {
    ++iteration;
    wait_for_resolution_phase();

    if (auto const errors{ collect_failed() }; !errors.empty()) {
      core_.fail_all();
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
        core_.fail_all();
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
        core_.fail_all();
        throw std::runtime_error("Dependency resolution made no progress at iteration " +
                                 std::to_string(iteration) + " with " +
                                 std::to_string(unresolved) + " unresolved references");
      }
      break;
    }
  }

  validate_product_fallbacks();
  validate_setup_selections();

  {  // Cache resolved weak dependency keys for thread-safe hash computation
    std::lock_guard const lock(mutex_);
    for (auto &[_, package] : packages_) {
      std::lock_guard const deps_lock(package->deps_mutex);
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

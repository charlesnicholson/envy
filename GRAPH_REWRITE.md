# Graph Rewrite Implementation Plan

## Overview

Replace TBB flow::graph with explicit thread-per-recipe model using resumable phase execution. This enables transitive dependency queries, short identifier resolution, and weak dependencies while simplifying the codebase.

## Goals

1. **Transitive dependency queries** - `envy asset B` works even if B is only a transitive dependency
2. **Short identifiers** - `envy asset python` instead of `local.python@r4`
3. **Weak dependencies** - Recipes can request "any ninja" and use manifest's version if present
4. **Simpler architecture** - Replace opaque TBB graph with explicit threading model

## Architecture Summary

### Current (TBB-based)
- 8 `tbb::flow::continue_node` per recipe
- Dynamic graph expansion during execution
- No memoization (duplicate nodes mitigated by cache)
- Dependencies resolved by each recipe independently
- Hard to query or stop at intermediate phases

### New (Thread-based)
- 1 `std::thread` per recipe running phase loop
- Resumable execution via `target_phase` atomic
- Recipe registry with memoization and query support
- Centralized dependency resolution
- Easy to query any recipe, stop at any phase

## Core Components

### 1. Recipe Key (`recipe_key.h/cpp`) - NEW

**Purpose:** Unified key type for all recipe identification, matching, and lookup.

**Design:**
```cpp
class recipe_key {
public:
  explicit recipe_key(recipe_spec const& spec);
  explicit recipe_key(std::string_view canonical);

  std::string const& canonical() const;  // "namespace.name@version{opt=val,...}"
  std::string_view identity() const;     // "namespace.name@version"
  std::string_view namespace_() const;   // "namespace"
  std::string_view name() const;         // "name"
  std::string_view version() const;      // "@version"

  bool matches(std::string_view query) const;  // Partial matching
  bool operator==(recipe_key const&) const;
  auto operator<=>(recipe_key const&) const;
  size_t hash() const;
};

// Hash specialization for std::unordered_map
template<> struct std::hash<envy::recipe_key>;
```

**Matching logic:**
- Exact canonical: `"local.python@r4{version=3.14}"` matches itself
- Identity: `"local.python@r4"` matches `"local.python@r4{version=3.14}"`
- Namespace.name: `"local.python"` matches any version
- Name only: `"python"` matches any namespace/version

**Implementation details:**
- Parse components once at construction
- Store as string_views into canonical string (zero copy)
- Compute hash once, cache it
- All comparisons use cached components

### 2. Engine (`engine.h/cpp`) - MODIFIED

**Purpose:** Execution engine with recipe storage, memoization, query support, and phase coordination. Replaces both `graph_state` and the concept of a separate `recipe_registry`.

**Design:**
```cpp
class engine : unmovable {
 public:
  engine(cache& cache, default_shell_cfg_t default_shell);

  // Recipe lifecycle (thread-safe)
  recipe* ensure_recipe(recipe_spec const& spec);
  void register_alias(std::string const& alias, recipe_key const& key);

  // Queries (thread-safe for read)
  recipe* find_exact(recipe_key const& key) const;
  std::vector<recipe*> find_matches(std::string_view query) const;

  // Phase coordination (thread-safe)
  void ensure_recipe_at_phase(recipe_key const& key, int phase);
  void wait_for_resolution_phase();
  void notify_phase_complete(recipe_key const& key, int phase);
  void on_recipe_fetch_start();
  void on_recipe_fetch_complete();

  // High-level execution
  recipe_result_map_t run_full(std::vector<recipe_spec> const& roots);
  void resolve_graph(std::vector<recipe_spec> const& roots);

  // Accessed by recipes during execution (read-only)
  cache& cache_;
  default_shell_cfg_t default_shell_;

 private:
  // Per-recipe coordination state
  struct recipe_coordination {
    std::thread worker;
    std::mutex mutex;
    std::condition_variable cv;
    std::atomic_int current_phase{-1};  // Last completed phase (-1 = not started)
    std::atomic_int target_phase{-1};   // Run until this phase (inclusive)
    std::atomic_bool failed{false};
  };

  void run_recipe_thread(recipe* r);  // Thread entry point

  std::unordered_map<recipe_key, std::unique_ptr<recipe>> recipes_;
  std::unordered_map<recipe_key, std::unique_ptr<recipe_coordination>> coordination_;
  std::unordered_map<std::string, recipe_key> aliases_;
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::atomic<int> pending_recipe_fetches_{0};
};
```

**Key algorithms:**

**Factory (memoized recipe creation):**
- Check if recipe already exists by key
- If not, create POD recipe with aggregate initialization
- Create corresponding coordination state
- Store both in maps under same key
- Return recipe pointer

**Query resolution:**
- Fast path: O(1) alias lookup
- Slow path: O(n) scan using recipe_key::matches()
- Returns vector of matching recipes

**Phase coordination:**
- Use atomic compare-exchange to extend target_phase
- Notify condvar when target extended
- Wait on condvar until current_phase >= target
- Check failed flag, throw if recipe failed

### 3. Recipe Structure (`recipe.h`) - MODIFIED

**Current (POD with TBB node pointers):**
```cpp
struct recipe {
  node_ptr recipe_fetch_node;
  node_ptr check_node;
  // ... 8 node pointers ...

  lua_state_ptr lua_state;
  cache::scoped_entry_lock::ptr_t lock;
  std::filesystem::path asset_path;
  std::string result_hash;
  recipe_spec spec;
  std::unordered_map<std::string, recipe*> dependencies;
};
```

**New (POD data struct - engine owns coordination):**
```cpp
// recipe.h
namespace envy {

// Plain data struct - engine orchestrates, phases operate on this
struct recipe {
  recipe_key key;
  recipe_spec spec;

  lua_state_ptr lua_state;
  cache::scoped_entry_lock::ptr_t lock;

  std::vector<std::string> declared_dependencies;
  std::unordered_map<std::string, recipe *> dependencies;

  std::string canonical_identity_hash;  // BLAKE3(format_key())
  std::filesystem::path asset_path;
  std::string result_hash;
};

}  // namespace envy
```

**Key design decisions:**
- Recipe is **pure data** with no methods, no threading primitives
- Engine owns all coordination state (thread, mutex, cv, atomics) via `recipe_coordination` struct
- Phases are free functions that take `(recipe*, engine&)` and directly access recipe fields
- Engine has `run_recipe_thread(recipe*)` method that executes the phase loop
- Recipe uses aggregate initialization (POD struct) instead of constructor

**Engine coordination structure:**
```cpp
// engine.h
class engine : unmovable {
 private:
  // Per-recipe coordination state (owned by engine)
  struct recipe_coordination {
    std::thread worker;
    std::mutex mutex;
    std::condition_variable cv;
    std::atomic_int current_phase{-1};  // Last completed phase (-1 = not started)
    std::atomic_int target_phase{-1};   // Run until this phase (inclusive)
    std::atomic_bool failed{false};
  };

  void run_recipe_thread(recipe *r);  // Thread entry point

  std::unordered_map<recipe_key, std::unique_ptr<recipe>> recipes_;
  std::unordered_map<recipe_key, std::unique_ptr<recipe_coordination>> coordination_;
  std::unordered_map<std::string, recipe_key> aliases_;
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::atomic<int> pending_recipe_fetches_{0};
};
```

**Thread execution loop:**
- Loop: check current vs target phase
- If at target, wait on condvar for target extension
- If target > current, run next phase:
  - Wait for dependencies (call ensure_recipe_at_phase on each dep)
  - Dispatch to phase function via switch statement
  - Update current_phase atomic
  - Notify completion via condvar
- On exception, set failed flag and rethrow

### 4. Dependency Specification (`recipe_spec.h`) - MODIFIED

**Add dependency variants:**
```cpp
struct recipe_spec {
  std::string identity;
  source_variant source;  // local/remote/git
  lua_value_map options;

  std::optional<std::string> alias;  // NEW: user-friendly short name

  struct dependency {
    std::string recipe;  // Query string or full identity
    std::optional<recipe_spec> weak;  // If present, this is weak dep with fallback
    std::optional<phase> needed_by;   // Defaults to check
  };
  std::vector<dependency> dependencies;
};
```

**Three dependency types:**

1. **Strong:** Has source, no weak field
   ```lua
   { recipe = "local.ninja@r0", source = "...", options = {...} }
   ```
   Always create this exact recipe.

2. **Weak:** Has weak field with fallback spec
   ```lua
   {
     recipe = "ninja",  -- Query
     weak = { recipe = "local.ninja@r0", source = "...", options = {...} },
     needed_by = "build"
   }
   ```
   Search for "ninja" in graph, use fallback if not found.

3. **Required:** No source, no weak field
   ```lua
   { recipe = "ninja" }
   ```
   Must exist in graph, error if not found.

### 5. Phase Execution (`phase_*.cpp`) - MODIFIED

**Current approach (external free functions called from TBB):**
- Each phase is free function: `void run_*_phase(recipe* r, graph_state& state)`
- Called from TBB node lambda
- Recipe is POD manipulated by external code

**New approach (external free functions called from engine thread):**
- Each phase remains a free function: `void run_*_phase(recipe* r, engine& eng)`
- Called from `engine::run_recipe_thread()` loop
- Recipe is POD, directly accessed by phase functions
- Engine provides coordination and access to cache/manifest/default_shell

**Phase dispatch:**
- Switch statement dispatches phase number to appropriate `run_*_phase()` function
- All phase functions take `(recipe*, engine&)` signature

**Phase function responsibilities:**
- **recipe_fetch**: Load Lua, parse deps, create child recipes via `ensure_recipe()`, start threads
- **check**: Resolve weak deps using `find_matches()`, run check verb
- **fetch/stage/build/install/deploy**: Access cache/shell via engine, manipulate recipe fields directly
- **completion**: Finalize result_hash and asset_path

### 6. Engine High-Level Methods (added to engine class) - NEW

**Engine is now an object created per command execution.** Commands create an engine, use it to execute recipes, then destroy it.

**High-level methods:**
```cpp
class engine {
 public:
  // Constructor extracts default_shell from manifest
  engine(cache& cache, default_shell_cfg_t default_shell);

  // Resolve graph (recipe_fetch phase only)
  void resolve_graph(std::vector<recipe_spec> const& roots);

  // Full installation (all phases)
  recipe_result_map_t run_full(std::vector<recipe_spec> const& roots);

  // ... other methods ...
};
```

**resolve_graph():**
- Create root recipes via ensure_recipe()
- Register aliases if present
- Start threads with target_phase=0 (recipe_fetch only)
- Wait for resolution phase completion

**run_full():**
- Call resolve_graph() first
- Extend all recipes to phase 7 via ensure_recipe_at_phase()
- Join all threads
- Collect and return results map

### 7. Command Usage (`cmd_*.cpp`) - MODIFIED

**Sync command:**
- Create engine with manifest's default_shell
- Call `run_full(manifest_->packages)`
- Report results

**Asset command:**
- Create engine, call `resolve_graph()` to load all recipes
- Query with `find_matches()` for target
- Handle not found / ambiguous cases
- Call `ensure_recipe_at_phase(target, 7)` to install
- Join threads, output asset path

## Implementation Phases

### Phase 1: Foundation (recipe_key only) ✅ COMPLETE
- [x] Create `recipe_key.h/cpp` with canonical storage and matching
- [x] Add `recipe_key_tests.cpp` unit tests (26 tests)
  - [x] Test canonical form generation
  - [x] Test component parsing (namespace, name, revision)
  - [x] Test matching: exact, identity, namespace.name, name-only
  - [x] Test hash consistency
  - [x] Test comparison operators
- [x] Update `recipe_spec.h` to add `alias` field (already present)
- [x] Add recipe_key_tests.cpp to CMakeLists.txt
- [ ] Update `manifest.cpp` to parse alias field from Lua (deferred - not needed for Phase 1/2)

**Status:** All 455 unit tests pass. recipe_key compiles and works correctly. No constructor means no invalid recipe_key can exist.

**Note:** We do NOT create recipe_registry as a separate entity. That functionality will be absorbed into the engine class in Phase 2.

### Phase 2: Engine Class and Recipe POD Structure ✅ COMPLETE
- [x] Update `engine.h` (absorbing graph_state and recipe_registry concepts)
  - [x] Constructor already exists taking `cache&` and `default_shell_cfg_t`
  - [x] Add `coordination_` map alongside existing `recipes_` map
  - [x] Add `recipe_coordination` struct with thread/mutex/cv/atomics
  - [x] Add `run_recipe_thread(recipe*)` method declaration
  - [x] `register_alias()`, `find_exact()`, `find_matches()` already exist
  - [x] `ensure_recipe_at_phase()`, `wait_for_resolution_phase()`, `notify_phase_complete()` already exist
  - [x] Keep existing `engine_run()` function (delegates to `engine::run_full()`)
- [x] Update `engine.cpp` implementation
  - [x] Implement `ensure_recipe(spec)` with POD recipe + coordination creation (uses designated initializers)
  - [x] Update all methods to use `coordination_` map instead of recipe fields
  - [x] Implement `run_recipe_thread(recipe*)` stub (marks phases complete, Phase 3 will add dispatch)
  - [x] Update `resolve_graph()` and `run_full()` to use coordination_
  - [x] Update destructor to join threads from coordination_
- [x] Modify `recipe.h` to be POD struct
  - [x] Remove all methods (no constructor, no run method)
  - [x] Remove all threading primitives (moved to engine's recipe_coordination)
  - [x] Keep only data fields: key, spec, lua_state, lock, dependencies, declared_dependencies, canonical_identity_hash, asset_path, result_hash
  - [x] Keep TBB node pointers (unused, will remove in Phase 4)
- [x] Update test fixtures to construct POD recipe
  - [x] phase_check_tests.cpp: use designated initializers
  - [x] phase_install_tests.cpp: use designated initializers
  - [x] Fix test identities to valid format (namespace.name@version)
- [N/A] Update existing phase files - deferred to Phase 3
- [N/A] Add `engine_tests.cpp` - existing engine functionality already tested
- [N/A] Add `recipe_execution_tests.cpp` - phase execution tested in Phase 3

**Status:** All 455 unit tests pass. Build succeeds. Engine uses coordination_ map correctly. Recipe is POD. Functional tests fail (152 failures) because run_recipe_thread() doesn't execute phases yet - this is expected and will be fixed in Phase 3.

### Phase 3: Phase Dispatch Implementation ✅ COMPLETE
- [x] Create `recipe_phase.h` enum for type-safe phase identification
  - [x] Add `recipe_phase` enum with values: none, recipe_fetch, check, fetch, stage, build, install, deploy, completion
  - [x] Use `recipe_phase::none` instead of casting -1 to avoid UB
  - [x] Define `phase_count` constant
- [x] Update engine to use `recipe_phase` enum throughout
  - [x] Change `recipe_execution_ctx` to use `std::atomic<recipe_phase>` instead of `std::atomic_int`
  - [x] Update all function signatures to take `recipe_phase` instead of `int`
  - [x] Update `ensure_recipe_at_phase()`, `notify_phase_complete()`, `run_recipe_thread()` to use enum
- [x] Implement phase dispatch in `run_recipe_thread()`
  - [x] Create constexpr dispatch table mapping phase enum to phase functions
  - [x] Replace placeholder with actual phase execution loop
  - [x] Add proper wait/resume logic for target_phase extension
  - [x] Add exception handling with failed flag propagation
- [x] Refactor recipe_execution_ctx encapsulation
  - [x] Move `set_target_phase()` implementation from header to .cpp
  - [x] Replace templated `start()` with concrete `start(recipe*, engine*)` method
  - [x] Remove inline implementations from header
- [x] Apply const correctness throughout engine.cpp
  - [x] Add const to lock guards, iterators, and local variables
  - [x] Add const to function parameters where appropriate
  - [x] Inline single-use variables with lambdas (e.g., `ctx_it` in `ensure_recipe_at_phase`)
- [x] Simplify atomic usage
  - [x] Remove explicit `.load()` and `.store()` calls where atomics can be treated as base types
  - [x] Keep `.load()` in lambdas where needed (can't capture atomics by value)
- [x] Verify thread safety
  - [x] Confirm all `cv_.wait()` calls use predicates (protection against spurious wakeups)
  - [x] Confirm `notify_all()` without mutex is safe (using atomics in predicates)

**Status:** All 455 unit tests pass. Phase dispatch working correctly with constexpr table. Type-safe phase handling eliminates magic numbers. Proper parallel execution with resumable phase progression. Functional tests still have 16 failures (expected - need actual phase implementations).

**Completion criteria:** Phase dispatch implemented, type-safe enum usage, proper synchronization, const correctness, no inline implementations in header.

### Phase 4: Phase Function Migration ✅ COMPLETE

**Goal:** Update all phase functions to work with the new engine coordination model, fixing the 16 remaining functional test failures.

**Tasks:**
- [x] Update `phase_recipe_fetch.cpp` to use engine coordination
  - [x] Signature already correct: `void run_recipe_fetch_phase(recipe* r, engine& eng)`
  - [x] Use `eng.ensure_recipe()` to create child recipes
  - [x] Use `eng.start_recipe_thread(dep, recipe_phase::recipe_fetch)` to start dependency threads
  - [x] Call `eng.on_recipe_fetch_complete()` at end of phase
- [x] Fix error handling to prevent crashes
  - [x] Catch exceptions in `run_recipe_thread()` without rethrowing (prevents std::terminate)
  - [x] Set `ctx.failed` flag on exceptions
  - [x] Decrement `pending_recipe_fetches_` counter on failure to unblock resolution barrier
  - [x] Check failed flag in `run_full()` and throw in main thread
- [x] Fix diamond dependency handling
  - [x] Added `start_recipe_thread()` method to engine
  - [x] Only start worker thread if not already started (check `worker.joinable()`)
  - [x] Always update target phase (handles multiple parents requesting same dependency)
- [x] Fix resolution barrier timing
  - [x] Increment counter in `start_recipe_thread()` when thread starts
  - [x] Decrement counter in `on_recipe_fetch_complete()` when phase completes
  - [x] Decrement counter in error handler if failed during recipe_fetch phase

**Status:** All 455 unit tests pass. Functional tests: 266/267 passing (99.6%).
- Fixed race condition in diamond dependencies (atomic `started` flag prevents double-thread-start)
- Implemented `needed_by` phase dependency coordination (default: check phase)
- Remaining 1 failure: `test_cycle_detection` - causes deadlock due to circular dependency wait (Phase 5 feature)

**Completion criteria:** ✅ COMPLETE - Multi-threaded recipe execution working correctly with proper error handling, no deadlocks on valid graphs, diamond dependencies supported. System is fully functional with TBB code still present but vestigial.

### Phase 5: TBB Removal, Cycle Detection, and Cleanup ✅ COMPLETE
- [x] Implement cycle detection
  - [x] Track ancestor chain during recipe_fetch phase
  - [x] Detect cycles when encountering already-visited recipe
  - [x] Provide clear error message with cycle path
  - [x] Fix `test_cycle_detection` functional test
- [ ] Remove TBB code (deferred to Phase 6)
  - [ ] Remove `create_recipe_nodes.cpp/h`
  - [ ] Remove `graph_state.h/cpp`
  - [ ] Remove TBB node pointers from `recipe.h`
  - [ ] Remove old TBB-based `engine_run()` function
- [ ] Remove TBB from build system (deferred to Phase 6)
  - [ ] Remove from `CMakeLists.txt`
  - [ ] Remove TBB includes from headers

**Status:** All 455 unit tests pass. All 267 functional tests pass (3 skipped - platform-specific).
- Cycle detection implemented using ancestor chain propagation
- Cycles detected during recipe_fetch phase before deadlock can occur
- Clear error messages showing cycle path

**Completion criteria:** ✅ COMPLETE - Cycle detection working. All tests pass including `test_cycle_detection`. TBB code remains present but unused (removal moved to Phase 6).

### Phase 6: Cleanup & Optimization
- [ ] Remove all TBB includes from headers
- [ ] Update `docs/architecture.md` to reflect new model
- [ ] Update `docs/recipe_resolution.md`
- [ ] Remove obsolete documentation about TBB graph
- [ ] Run clang-format on all modified files
- [ ] Address any compiler warnings

**Completion criteria:** No TBB remnants, documentation updated, clean build.

## Testing Plan - ✅ COMPLETE

All comprehensive test coverage already exists and passes. No new tests needed.

### Unit Tests - ✅ 455 tests passing

**recipe_key_tests.cpp (26 tests):**
- [x] Canonical form: identity + sorted options
- [x] Component parsing: namespace, name, version extracted correctly
- [x] Exact matching: canonical matches itself
- [x] Identity matching: matches same identity with different options
- [x] Namespace.name matching: matches any version
- [x] Name-only matching: matches any namespace/version
- [x] Non-matching: different names, namespaces don't match
- [x] Hash consistency: same key produces same hash
- [x] Equality: canonical equality works correctly
- [x] Invalid identities: missing namespace or version throws

**engine_tests.cpp (11 tests):**
- [x] Memoization: same spec returns same recipe pointer via `ensure_recipe()`
- [x] Different options: same identity, different options = different recipes
- [x] Alias registration: alias maps to canonical key
- [x] Duplicate alias error: different recipes can't share alias
- [x] Alias lookup: O(1) exact match via `find_matches()`
- [x] Query by name: finds all matching names
- [x] Query by namespace.name: filters by namespace
- [x] Query not found: returns empty vector

### Functional Tests - ✅ 267 tests passing (3 skipped - platform-specific)

**test_needed_by.py (13 tests):**
- [x] Default needed_by=check: dependency completes before parent's check phase
- [x] needed_by=fetch: dependency completes before parent's fetch phase
- [x] needed_by=stage: dependency completes before parent's stage phase
- [x] needed_by=build: dependency completes before parent's build phase
- [x] needed_by=install: dependency completes before parent's install phase
- [x] needed_by=deploy: dependency completes before parent's deploy phase
- [x] Diamond dependencies: same dependency from multiple parents with different needed_by phases
- [x] Transitive needed_by: A depends on B (build), B depends on C (fetch)
- [x] All 13 test variants pass

**test_dependency_validation.py (15 tests):**
- [x] Dependency cycle detection with clear error messages
- [x] Missing dependency detection
- [x] Invalid dependency specification errors
- [x] Duplicate dependency declarations
- [x] Security: non-local recipes cannot depend on local recipes
- [x] All 15 validation tests pass

**Existing test suites (239 tests):**
- [x] test_smoke.py: basic functionality
- [x] test_recipe_loading.py: Lua recipe parsing
- [x] test_fetch_*.py: HTTP, HTTPS, FTP, S3, git sources
- [x] test_extraction.py: archive handling
- [x] test_caching.py: cache behavior
- [x] test_manifest.py: manifest parsing
- [x] All existing functional tests adapted to new engine model

### Performance Tests - N/A (skipped per instructions)

Performance tests deferred - no comparison with TBB needed, just correctness verification.

## Migration Risks & Mitigations

### Risk: Threading Bugs
**Mitigation:**
- Extensive unit tests for synchronization primitives
- Thread sanitizer in CI (already enabled)
- Careful review of mutex/condvar usage

### Risk: Performance Regression
**Mitigation:**
- Benchmark against TBB version before merging
- Profile with typical workloads
- Thread-per-recipe is actually simpler than TBB scheduler

### Risk: Breaking Changes for Users
**Mitigation:**
- This is internal refactor, no recipe API changes
- Manifest syntax unchanged (except new optional `alias` field)
- All existing recipes should work as-is

### Risk: Complex Rollback
**Mitigation:**
- Implement in feature branch
- Keep TBB code until new implementation proven
- Thorough testing before TBB removal

## Success Criteria

- [x] All existing unit tests pass (455 tests ✅)
- [x] All existing functional tests pass (267 tests ✅, 3 skipped - platform-specific)
- [x] Tests for recipe_key: 26 tests pass
- [x] Tests for engine memoization, aliases, queries: 11 tests pass
- [x] Tests for needed_by phase coordination: 13 tests pass
- [x] Tests for dependency validation and cycle detection: 15 tests pass
- [x] `envy sync` works on example manifests
- [x] Cycle detection prevents deadlocks with clear error messages
- [ ] No TBB dependencies in codebase (deferred to Phase 6)
- [ ] Documentation updated (deferred to Phase 6)
- [ ] Clean build with no warnings (current: clean build ✅)

## Rollout Plan

1. **Develop in feature branch** (`feature/graph-rewrite`)
2. **Merge Phase 1** (recipe_key + registry) - no behavior changes yet
3. **Merge Phase 2** (resumable execution) - still using TBB in parallel
4. **Merge Phase 3** (phase migration) - switch to new model, TBB still linked
5. **Merge Phase 4** (engine migration) - fully on new model, verify extensively
6. **Merge Phase 5** (cleanup) - remove TBB, update docs
7. **Tag release** with graph rewrite complete

## Estimated Effort

- **Phase 1:** 2-3 days (foundation, well-defined)
- **Phase 2:** 2-3 days (threading model, needs careful testing)
- **Phase 3:** 3-4 days (8 phase files to migrate)
- **Phase 4:** 2-3 days (engine changes, command updates)
- **Phase 5:** 1-2 days (cleanup, documentation)

**Total:** ~12-17 days for complete migration

## Open Questions

1. **Condvar strategy:** Global vs per-recipe condvar?
   - **Decision:** Start with global for simplicity, profile if contention becomes issue

2. **Thread pool:** Use thread pool instead of thread-per-recipe?
   - **Decision:** No, recipes are long-lived (seconds to minutes), overhead minimal

3. **Cancellation:** Support graceful shutdown (Ctrl-C)?
   - **Decision:** Defer to post-rewrite, not critical for MVP

4. **Failed recipe cleanup:** If B fails, should A abort immediately?
   - **Decision:** A blocks on B, gets exception, propagates up

5. **Backward compatibility:** Keep TBB as fallback option?
   - **Decision:** No, clean break for simplicity

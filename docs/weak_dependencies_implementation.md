# Weak Dependencies & Recipe Fetch Prerequisites Implementation Plan

## Overview

Two complementary features:

1. **Nested Source Dependencies**: Recipes declare prerequisites for fetching dependencies' recipes (e.g., "install jfrog CLI before fetching corporate toolchain recipe")
2. **Weak Dependencies**: Recipes reference dependencies without complete specs—error if missing (reference-only) or use fallback (weak)

Both integrate via iterative graph expansion/resolution with convergence.

## Implementation Status

- ✅ **Phase 0**: Phase Enum Unification (complete)
- ✅ **Phase 1**: Shared Fetch API (complete)
- ✅ **Phase 2**: Nested Source Dependencies (complete)
- ⚠️ **Phase 3**: Weak Dependency Resolution (core resolution implemented; edge-cases/tests pending)
- ❌ **Phase 4**: Integration (not started)
- ❌ **Phase 5**: Documentation & Polish (not started)

**Current State**: Nested source dependencies fully implemented and tested (Phase 2 complete). Weak dependency parsing, collection, and core resolution loop are implemented and wired into `resolve_graph`; ambiguity/missing reporting works but stuck detection + dedicated tests are still pending.

---

## Terminology

**Reference-only dependency**: Partial identity match (e.g., `{recipe = "python"}`) without source or fallback. Recipe declares "I need something matching 'python' but won't tell you how to get it—someone else must provide it." Errors if no match found after resolution.

**Weak dependency**: Partial identity match with fallback recipe (e.g., `{recipe = "python", weak = {...}}`). Recipe declares "I prefer something matching 'python' from elsewhere, but here's a fallback if not found." Uses fallback only if no match exists after strong closure.

**Partial identity matching**: Query matches namespace/name/revision (ignoring options). Examples:
- `"python"` matches `*.python@*` (any namespace, any revision, any options)
- `"local.python@r4"` matches `local.python@r4{...}` (exact identity, any options)
- Ambiguity (multiple matches with different options/revisions) → error

**Nested source dependency**: Prerequisite for fetching a recipe's source (e.g., "install jfrog before fetching toolchain recipe from Artifactory"). Declared in `source.dependencies`, must complete before outer recipe can be fetched.

---

## Core Concepts

### Nested Source Dependencies

**Status**: Parsing ✅ implemented, Execution ❌ not yet implemented

**Supported Syntax** (parsing complete):
```lua
source = {
  dependencies = {  -- Prerequisites for fetching this recipe
    { recipe = "jfrog.cli@v2", source = "...", sha256 = "..." }
  },
  fetch = function(ctx)
    -- Fetch function body (execution not yet implemented)
    -- Will have access to ctx:asset(), ctx:fetch(), etc.
  end
}
```

**Semantics**: "To fetch this recipe, first install dependencies, then run custom fetch function."

**Implicit needed_by**: Fetch dependencies must reach `completion` phase before outer recipe's recipe_fetch runs.

### Weak Dependencies

**Status**: ❌ NOT IMPLEMENTED (Phase 3)

**Planned Syntax**:
```lua
dependencies = {
  { recipe = "python" },  -- Reference-only: error if not found

  {
    recipe = "python",  -- Weak: use fallback if not found
    weak = { source = "...", options = {...} }
  }
}
```

**Planned Semantics**: Reference matches via partial identity (namespace/name/revision, options ignored). Weak provides fallback if no match exists after strong closure.

### Integration

**Status**: ❌ NOT IMPLEMENTED (Phase 4)

Nested source dependencies will support weak references:
```lua
source = {
  dependencies = {
    { recipe = "jfrog.cli", weak = { source = "...", ... } }
  },
  fetch = function(ctx) ... end
}
```

---

## Phase 0: Phase Enum Unification ✅ COMPLETE

### Goal

Unify `phase` and `recipe_phase` enums into single consistent type system. Current codebase has two enums (`src/phase.h` and `src/recipe_phase.h`) causing type confusion in `needed_by` fields and phase coordination.

### Completion Summary

**Status**: All tasks completed successfully. Build passes with 466/466 unit tests passing.

---

## Phase 1: Shared Fetch API - ✅ COMPLETE

### Goal

Move fetch helpers to `lua_ctx_bindings` for use across asset fetch and recipe fetch phases. Eliminates duplication while maintaining existing architecture.

### Status

✅ **Implemented**: `lua_ctx_bindings_register_fetch_phase()` provides two-step fetch pattern (ungated download to tmp, gated commit with SHA256 verification)

✅ **In use**: Asset fetch phase (phase_fetch.cpp) uses this API

❌ **Pending**: Recipe fetch context needs to use this API once custom fetch execution is implemented (Phase 2)

---

## Phase 2: Nested Source Dependencies (Recipe Fetch Prerequisites) - ✅ COMPLETE

### Goal

Enable recipes to declare dependencies needed for fetching other recipes' Lua files.

### Status Summary

- ✅ Parsing: recipe_spec parses `source.dependencies` and validates constraints
- ✅ Fetch dependency processing: `process_fetch_dependencies()` adds fetch deps to dependency map
- ✅ Custom fetch execution: phase_recipe_fetch handles fetch_function sources with parent pointer lookup
- ✅ Context API: fetch_phase_ctx with ctx.fetch() and ctx.commit_fetch() implemented
- ✅ Functional tests: Comprehensive test suite covering simple, multi-level, and multiple fetch dependencies

### Key Implementation Details

**recipe_spec changes** (src/recipe_spec.h/cpp):
- Added `fetch_function{}` empty struct variant to source_t
- Added `source_dependencies` field for nested prerequisites
- parse_source_table() validates and recursively parses dependencies
- lookup_and_push_source_fetch() dynamically retrieves functions (no caching)

**engine changes** (src/engine.h/cpp):
- process_fetch_dependencies() helper (engine.cpp:205-244)
- Called from run_recipe_thread() before phase loop (engine.cpp:256-258)
- Adds fetch deps to r->dependencies with needed_by=recipe_fetch
- Cycle detection via ancestor chain (same as regular deps)
- Phase loop's existing wait logic handles blocking automatically

**Design principle**: recipe_spec remains POD-like. No lua_State pointers cached. Functions looked up on-demand from owning recipe's lua_State. Fetch deps are regular dependencies—no special casing.

### Execution Flow

Fetch dependency processing happens in `run_recipe_thread()` BEFORE the phase loop begins. No special casing—fetch deps added to `r->dependencies` map with `needed_by = recipe_phase::recipe_fetch`, then existing phase coordination logic handles blocking.

**Execution flow:**

```cpp
void engine::run_recipe_thread(recipe *r) {
  auto &ctx{get_execution_ctx(r)};

  // Process fetch dependencies BEFORE phase loop
  if (!r->spec->source_dependencies.empty()) {
    for (auto &fetch_dep_spec : r->spec->source_dependencies) {
      // Cycle detection: check against ancestor chain
      if (r->spec->identity == fetch_dep_spec.identity) {
        error("Self-loop: " + r->spec->identity + " → " + fetch_dep_spec.identity);
      }

      for (auto const &ancestor : ctx.ancestor_chain) {
        if (ancestor == fetch_dep_spec.identity) {
          error("Fetch dependency cycle: " + build_cycle_path(...));
        }
      }

      recipe *fetch_dep{ensure_recipe(&fetch_dep_spec)};

      // Add to dependencies map - phase loop will handle blocking
      r->dependencies[fetch_dep_spec.identity] = {fetch_dep, recipe_phase::recipe_fetch};
      ENVY_TRACE_FETCH_DEPENDENCY_ADDED(r->spec->identity, fetch_dep_spec.identity);

      // Build child chain, start thread to completion
      std::vector<std::string> child_chain{ctx.ancestor_chain};
      child_chain.push_back(r->spec->identity);
      start_recipe_thread(fetch_dep, recipe_phase::completion, std::move(child_chain));
    }
  }

  try {
    while (true) {  // Phase loop begins
      recipe_phase const target{ctx.target_phase};
      recipe_phase const current{ctx.current_phase};

      if (current >= target) { /* wait for target extension */ }

      recipe_phase const next{current + 1};

      // Wait for dependencies needed by this phase (EXISTING LOGIC)
      for (auto const &[dep_identity, dep_info] : r->dependencies) {
        if (next >= dep_info.needed_by) {
          // Fetch deps have needed_by=recipe_fetch, so they block here
          ensure_recipe_at_phase(dep_info.recipe_ptr->key, recipe_phase::completion);
        }
      }

      // Run phase - fetch deps guaranteed complete by this point
      phase_dispatch_table[next](r, *this);
      // ...
```

**Key insight:** Fetch dependencies are just regular dependencies with `needed_by = recipe_phase::recipe_fetch`. The recipe thread starts, populates its dependency map, then enters the phase loop. When advancing to `recipe_fetch`, the existing dependency wait logic blocks until fetch deps reach `completion`. No special paths.

**Custom fetch execution** (in `phase_recipe_fetch`):

When `spec.source` is `fetch_function` variant:
1. Fetch deps already complete (guaranteed by phase loop wait)
2. Create `fetch_phase_ctx` with fetch_dir (internal), run_dir (tmp_dir exposed), stage_dir
3. Register fetch bindings via `lua_ctx_bindings_register_fetch_phase()` (ctx.fetch + ctx.commit_fetch)
4. Call custom fetch function via `lookup_and_push_source_fetch()`
5. Custom fetch writes recipe.lua to tmp_dir, optionally uses ctx.fetch/commit_fetch for downloads
6. Load `cache.recipes_dir / identity / recipe.lua` (committed by custom fetch), continue normal processing

**Security boundary**: fetch_dir never exposed to Lua—only accessible via ctx.commit_fetch() which enforces SHA256 verification.

### Tasks

- [x] 2.1: Extend `recipe_spec` to support nested source dependencies
  - ✅ Added `fetch_function` empty struct variant to `source_t`
  - ✅ Added `source_dependencies` field (std::vector<recipe_spec>)
  - ✅ Parse in `parse_source_table()` when source table has "dependencies" field
  - ✅ Recursive parsing via `recipe_spec::parse()`

- [x] 2.2: Validate `source.dependencies` constraints
  - ✅ Error if `source.dependencies` present but `source.fetch` not function
  - ✅ Error if `source.dependencies` not a table
  - ✅ Error if source table has neither dependencies nor fetch
  - ✅ Unit tests in recipe_spec_custom_source_tests.cpp validate all constraints

- [x] 2.3: Implement fetch dependency processing in `run_recipe_thread()`
  - ✅ Extracted `process_fetch_dependencies()` helper (engine.cpp:205-244)
  - ✅ Called from `run_recipe_thread()` before phase loop (engine.cpp:256-258)
  - ✅ Cycle detection: self-loops and ancestor chain checks
  - ✅ Adds fetch deps to `r->dependencies` map with `needed_by = recipe_phase::recipe_fetch`
  - ✅ Starts fetch dep threads to `completion` phase
  - ✅ Emits `ENVY_TRACE_DEPENDENCY_ADDED` events
  - ✅ Phase loop's existing wait logic (lines 280-290) blocks recipe_fetch until deps complete
  - ⚠️ Unit tests: cycle detection not yet extracted to pure function (defer to Task 2.6)

- [x] 2.4: Create context and register fetch bindings for custom fetch
  - ✅ Use existing `fetch_phase_ctx` from Phase 1 (no new struct needed)
  - ✅ Set paths: `fetch_dir` (internal only), `run_dir` (tmp_dir), `stage_dir`, `engine_`, `recipe_`
  - ✅ Create Lua context table with `identity` and `tmp_dir` fields
  - ✅ `identity`: recipe's canonical identity string (e.g., "local.python@r4")—exposed in all recipe phase contexts
  - ✅ `tmp_dir`: ephemeral working directory—files deleted after phase unless explicitly committed
  - ✅ **Note**: Manifest scripts don't get `identity` field since manifests aren't recipes
  - ✅ Call `lua_ctx_bindings_register_fetch_phase()` to register `ctx.fetch()` and `ctx.commit_fetch()`
  - ✅ **Security boundary**: fetch_dir NOT exposed to Lua—only accessible via commit_fetch()
  - ✅ Two-step pattern: ctx.fetch() downloads to tmp_dir, ctx.commit_fetch() verifies SHA256 and moves to fetch_dir
  - ✅ Implementation: phase_recipe_fetch.cpp:186-265

- [x] 2.5: Handle `fetch_function` variant in `phase_recipe_fetch`
  - ✅ Location: phase_recipe_fetch.cpp:186, added `else if (spec.has_fetch_function())` branch
  - ✅ Fetch deps guaranteed complete by this point (blocked in phase loop)
  - ✅ Create `fetch_phase_ctx` with cache paths (Task 2.4)
  - ✅ Look up custom fetch via `lookup_and_push_source_fetch()`, execute with `sol::protected_function`
  - ✅ Pass context table as first argument (ctx), options as second argument
  - ✅ Custom fetch writes to tmp_dir, uses ctx.fetch/commit_fetch for downloads
  - ✅ After function returns: load `cache.recipes_dir / identity / recipe.lua` (committed by custom fetch)
  - ✅ Continue to line 270 equivalent (parse dependencies from loaded recipe.lua)
  - ✅ Trace events: reuse existing `lua_ctx_fetch_start/complete` (emitted by ctx.fetch)
  - ✅ Helper function: `find_owning_recipe()` locates parent Lua state for fetch function lookup

- [x] 2.6: Cycle detection for fetch dependencies
  - ✅ Integrated into Task 2.3—same mechanism as regular deps (engine.cpp:210-221)
  - ✅ Extracted to testable function: `validate_dependency_cycle()` (engine.cpp:38-57)
  - ✅ Unit tests: engine_tests.cpp has no cycle, direct cycle, self-loop, annotated error messages
  - ✅ Functional test: test_fetch_dependency_cycle covers A fetch needs B, B fetch needs A cycle

- [x] 2.7: Functional tests for nested source dependencies
  - ✅ Test: Simple (A fetch needs B) - validates basic flow, blocking, completion
  - ✅ Test: Multi-level nesting (A fetch needs B, B fetch needs C) - validates deeply nested custom fetch
  - ✅ Test: Multiple fetch deps (A fetch needs [B, C]) - validates parallel fetch dependencies
  - ✅ Test: Cycle detection (A fetch needs B, B fetch needs A) - existing test_fetch_dependency_cycle covers this
  - ✅ Context API (ctx.commit_fetch()) used and validated in all custom fetch tests
  - Test recipes: test_data/recipes/{simple_fetch_dep_parent,multi_level_a,multiple_fetch_deps_parent,fetch_dep_helper}.lua
  - Functional tests: functional_tests/test_engine_dependency_resolution.py

**Testing strategy:**
- Unit tests: Parsing (done), cycle detection (extract to pure function)
- Functional tests: Threading, blocking, phase coordination, graph traversal
- Rationale: Engine threading/CV behavior not unit-testable without major refactoring; functional tests adequate (582 tests run in ~20s)

**Notes**:
- Old `needed_by="recipe_fetch"` manifest syntax is deprecated. Use `source.dependencies` instead. No validation task needed—old syntax can be removed in separate cleanup.
- Alias feature removed (recipe_spec::alias field, engine::register_alias(), engine::aliases_ map, all tests). Was never implemented/used. Replacement feature planned after weak dependencies complete.

---

## Phase 3: Weak Dependency Resolution - ⚠️ IN PROGRESS

### Goal

Enable reference-only and weak dependencies with iterative graph resolution.

### Current Implementation

- Data model/parsing: dependencies with no `source` are treated as weak references. They capture the query, optional fallback `recipe_spec`, and `needed_by` (defaults to `asset_build`). Fallback blocks are validated as mutual exclusive with `source` and cannot carry `needed_by`.
- Collection: `phase_recipe_fetch` appends weak references to `recipe::weak_references` and skips instantiation. Strong deps still create recipes + threads and populate `declared_dependencies`.
- Resolution loop: `resolve_graph` starts all roots to `recipe_fetch` and then loops while weak resolution or fallback instantiation makes progress:
  - `wait_for_resolution_phase()` blocks until all recipes targeting `recipe_fetch` finish (includes any newly started fallbacks).
  - Call `resolve_weak_references()`; it returns counts of resolved refs and started fallbacks.
  - Continue the loop while either count is non-zero (so progress is recognized even if new weak refs keep the unresolved total flat).
- `resolve_weak_references()` behavior:
  - Uses `find_matches()` (engine-level, sees all recipes) to resolve a single match: wires dependency (with `needed_by`), extends `declared_dependencies`, and marks resolved.
  - Multiple matches: collect ambiguity messages; no wiring changes.
  - No matches with fallback: set parent, `ensure_recipe()` the fallback, wire dependency + declared deps, start a `recipe_fetch` thread for it, mark resolved, clear fallback pointer.
  - No matches and no fallback: left unresolved for final validation.
  - After the pass, aggregate all ambiguity + missing-reference messages and throw if any exist (so errors are reported after the pass completes).
- Thread safety: resolution only runs after all current `recipe_fetch` targets complete; fallbacks are started in a batch, and the next loop waits for them to finish before resolving again. No new threading primitives were added.
- Gaps: no explicit stuck detection yet (loop stops when unresolved count stops decreasing), and no targeted unit/functional tests for weak resolution. Nested weak fetch-deps integration is still pending (Phase 4).

### Tasks

- [x] 3.1: Extend `recipe_spec` for weak dependencies
  - Added `weak` fallback (owned `unique_ptr<recipe_spec>`) and `weak_ref` source variant
  - Parse: `{ recipe = "...", weak = {...} }` (weak) or `{ recipe = "..." }` (reference-only)
  - Validate: `weak` and `source` mutually exclusive; `weak` cannot carry `needed_by`

- [x] 3.2: Add `weak_references` to recipe struct
  - Added `std::vector<weak_reference>` (query, fallback ptr, needed_by, resolved)

- [x] 3.3: Collect weak refs in `phase_recipe_fetch`
  - Dependencies without `source` are recorded into `recipe::weak_references` with query, needed_by, and moved fallback (strong deps unchanged)
  - Strong deps still `ensure_recipe()` + start threads

- [x] 3.4: Implement `engine::resolve_weak_references()`
  - Resolves single matches, wires dependencies/declared deps, starts fallbacks, aggregates ambiguity/missing messages, and throws after a full pass

- [x] 3.5: Integrate into `engine::resolve_graph()`
  - Core loop now lives in `resolve_graph`: `while (progress) { wait_for_resolution_phase(); resolve_weak_references(); }`
  - Progress measured by unresolved weak-ref count; new fallbacks join the next iteration

- [ ] 3.6: Error handling polish
  - Improve stuck detection/reporting when unresolved count stops changing
  - Ensure ambiguity and missing messages stay clear and deduped

- [ ] 3.7: Unit tests for weak resolution logic
  - Test: single weak ref, no match → fallback instantiated
  - Test: weak ref, match exists → fallback ignored
  - Test: reference-only, match exists → resolves
  - Test: reference-only, no match → error after convergence
  - Test: ambiguous match (two candidates) → error with list
  - Test: convergence after 2 iterations (cascading weak)
  - Test: Stuck detection (when added)
  - Test: threads stay parked at recipe_fetch during resolution; targets extend only after convergence

- [ ] 3.8: Functional tests for weak dependencies
  - Test: Simple weak (A-weak→B{v=1}, no B in manifest → B{v=1} used)
  - Test: Strong overrides weak (manifest has B{v=2}, A-weak→B{v=1} → B{v=2} used)
  - Test: Ambiguity error (A-weak→C{v=1}, B-weak→C{v=2} → error with candidates)
  - Test: Transitive satisfaction (A-weak→B, B-strong→C, D-weak→C → D uses B's C)
  - Test: Coexistence (A-strong→C{v=1}, B-strong→C{v=2} → both build independently)
  - Test: Cascading weak (A-weak→B, B-weak→C, C-weak→D → 3 iterations)
  - Test: Reference-only satisfied by weak (A-ref→"B", C-weak→B → C's weak satisfies A)
  - Test: Partial matching (A-ref→"python" matches "local.python@r4")
  - Test: Progress despite flat unresolved count (two fallbacks each introduce weak refs; loop continues because fallbacks_started > 0)

---

## Phase 4: Integration (Nested Source + Weak) - ❌ NOT STARTED

### Goal

Nested source dependencies can be weak, enabling recipes to express "if jfrog not in graph, use fallback before fetching my recipe."

**Execution model**: Iterative progression algorithm with distinct phases:
1. **Strong closure**: Resolve all strong references, accumulate weak/reference-only deps
2. **Weak resolution waves**: Resolve unresolved refs → fetch fallbacks → resolve new refs → repeat until convergence
3. **Ambiguity validation**: Error if any weak resolutions are ambiguous (same recipe, different revision/options)

### Algorithm

**Parsing** (extends Phase 2 + Phase 3):
```
parse_recipe_spec(lua_value):
  if source is table with "dependencies":
    fetch_deps = []
    for dep_val in source.dependencies:
      dep_spec = parse_recipe_spec(dep_val)  # May lack source (weak/reference-only)
      fetch_deps.append(dep_spec)

    # Fetch deps can be weak
    return recipe_spec{
      source = custom_fetch{
        dependencies: fetch_deps,  # Contains weak refs
        function: fetch_func
      }
    }
```

**Execution** (during weak resolution):
```
# When processing nested source dependencies during phase_recipe_fetch:
for fetch_dep_spec in outer_dep.source.dependencies:
  if !fetch_dep_spec.has_source():
    # Nested fetch dep is weak/reference-only
    # Store in unresolved set, resolve during weak resolution phase
    unresolved.add((outer_recipe, weak_reference{
      identity: fetch_dep_spec.identity,
      weak_fallback_spec: fetch_dep_spec.weak,
      context: "nested_fetch_dep"
    }))
  else:
    # Strong nested fetch dep (Phase 2 behavior)
    fetch_dep = ensure_recipe(fetch_dep_spec)
    start_recipe_thread(fetch_dep, recipe_fetch)
    wait_for_completion(fetch_dep)
```

### Tasks

- [ ] 4.1: Update nested source dependency parsing to handle weak refs
  - Nested `source.dependencies` entries can lack `source` field (weak/reference-only)
  - Parse weak nested fetch deps same as top-level weak deps
  - Unit test: parse `source = { dependencies = { { recipe = "...", weak = {...} } }, fetch = function }`

- [ ] 4.2: Collect weak nested fetch deps in `phase_recipe_fetch`
  - When processing `dep_spec.source.dependencies`: check if `source` field absent
  - If no source: add to `unresolved` set (resolve later)
  - If has source: install immediately (Phase 2 behavior)
  - Annotate context: "nested fetch dep for {outer_identity}"

- [ ] 4.3: Resolve nested weak fetch deps during weak resolution
  - Nested weak refs participate in convergence loop
  - Once resolved: install fetch dep, then fetch outer recipe
  - Error if nested reference-only not found after convergence

- [ ] 4.4: Functional tests for integrated behavior
  - Test: Nested weak used (toolchain fetch needs jfrog weak, jfrog not in graph → weak instantiated)
  - Test: Nested weak ignored (jfrog in graph → nested weak ignored, graph's jfrog used)
  - Test: Nested reference-only (toolchain fetch refs "jfrog", jfrog provided by manifest)
  - Test: Nested ambiguity (two jfrogs in graph, nested ref ambiguous → error)
  - Test: Complex nesting (A→B fetch needs C weak, C weak brings D strong, E refs D)

---

## Phase 5: Documentation & Polish - ❌ NOT STARTED

### Tasks

- [ ] 5.1: Update `docs/architecture.md`
  - Add "Nested Source Dependencies" section with JFrog example
  - Add "Weak Dependencies" section with weak syntax (recipe + weak field)
  - Document iterative progression model (strong closure → weak resolution waves)
  - Document convergence algorithm, ambiguity resolution, stuck detection
  - Remove TBB references throughout (outdated - envy uses std::thread now)
  - Note: `needed_by="recipe_fetch"` is deprecated syntax, use `source.dependencies` instead

- [ ] 5.2: Update `docs/recipe_resolution.md`
  - Explain two-phase resolution (strong closure, then weak convergence)
  - Partial identity matching rules with examples
  - Transitive satisfaction semantics
  - Coexistence vs ambiguity (when error, when allow)

- [ ] 5.3: Add examples to `test_data/recipes/`
  - Example: JFrog bootstrap pattern
  - Example: Weak dependency with fallback
  - Example: Reference-only dependency
  - Example: Nested weak fetch dependency

- [ ] 5.4: Error message review
  - Ambiguous reference: clear candidate list (show all candidates with full keys)
  - Ambiguous with options: explain partial matching ignores options
  - Reference-only not found: suggest adding to manifest or using weak fallback
  - Stuck after 3 iterations: suggest missing deps or circular weak refs, iteration count in message

- [ ] 5.5: Performance validation
  - Profile `find_matches()` with 100+ recipe graph
  - If slow: implement single identity map optimization (defer full multi-index)
  - Measure weak resolution convergence time (typical case: 1-2 iterations)

---

## Testing Strategy

### Unit Tests

**Shared Fetch API** (Phase 1):
- Parse/execute: `ctx.fetch()` with SHA256
- Parse/execute: `ctx.import_file()` with SHA256
- Error: SHA256 mismatch
- Concurrent batch downloads

**Nested Source Dependencies** (Phase 2):
- Parse: `source = { dependencies = [...], fetch = function }`
- Parse error: `source.dependencies` without `source.fetch` function
- Execute: fetch dep installed before outer recipe fetch
- Cycle detection in nested fetch deps
- Validation: `needed_by="recipe_fetch"` in recipe file → error

**Weak Resolution** (Phase 3):
- Parse: `{ recipe = "..." }` (reference-only), `{ recipe = "...", weak = {...} }`
- Parse error: `{ recipe = "...", source = "...", weak = {...} }` (source+weak exclusive)
- Resolve: weak fallback used when no match
- Resolve: weak ignored when match exists
- Resolve: reference-only error when no match after convergence
- Error: ambiguous (multiple matches)
- Convergence: cascading weak (2-3 iterations)

**Integration** (Phase 4):
- Parse: nested weak fetch dep
- Execute: nested weak resolved, then fetch outer recipe
- Error: nested reference-only not found

### Functional Tests

**Nested Source Dependencies**:
- JFrog bootstrap: A→toolchain, toolchain fetch needs jfrog
- Multi-level: A→B fetch needs C, C fetch needs D
- Parallel fetch deps: A→B fetch needs [C, D] (both install concurrently)
- Cycle: A→B fetch needs C, C→D fetch needs A → error

**Weak Dependencies**:
- Simple weak: A-weak→B, no B → B instantiated
- Strong overrides: manifest has B, A-weak→B → manifest's B used
- Ambiguity: A-weak→C{v=1}, B-weak→C{v=2} → error with candidates
- Transitive: A-weak→B, B-strong→C, D-weak→C → D uses B's C
- Coexistence: A-strong→C{v=1}, B-strong→C{v=2} → both build
- Cascading: A-weak→B, B-weak→C → 2 iterations
- Reference satisfied by weak: A-ref→"B", C-weak→B → works

**Integration**:
- Nested weak used: toolchain fetch needs jfrog weak, jfrog not in graph
- Nested weak ignored: toolchain fetch needs jfrog weak, jfrog in graph
- Complex: A→B fetch needs C weak, C brings D strong, E refs D

### Validation Tests

- Nested source without fetch function → parse error
- Reference + recipe in same dep → parse error
- Stuck after 3 no-progress iterations → error with iteration count and unresolved count

---

## Implementation Order

1. **Phase 0** - Phase Enum Unification (prerequisite cleanup)
2. **Phase 1** - Shared Fetch API (foundation for both features)
3. **Phase 2** - Nested Source Dependencies (establish robust fetch-time dep handling)
4. **Phase 3** - Weak Resolution (iterative convergence algorithm)
5. **Phase 4** - Integration (nested + weak together)
6. **Phase 5** - Documentation & Polish

Each phase independently testable, can ship incrementally if needed.

---

## References

**Existing Code**:
- `src/engine.h/.cpp` - Graph resolution, threading model
- `src/engine_phases/phase_recipe_fetch.cpp` - Recipe fetch execution
- `src/recipe_spec.h/.cpp` - Recipe spec parsing
- `src/recipe_key.h/.cpp` - Partial identity matching (`matches()`)
- `src/cache.cpp` - Fetch directory, per-file caching

**Related Docs**:
- `docs/architecture.md` - Current resolution model
- `docs/recipe_resolution.md` - Dependency semantics
- `docs/cache.md` - Cache layout, fetch/work directories

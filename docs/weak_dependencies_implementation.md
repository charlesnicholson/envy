# Weak Dependencies & Recipe Fetch Prerequisites Implementation Plan

## Overview

Two complementary features:

1. **Nested Source Dependencies**: Recipes declare prerequisites for fetching dependencies' recipes (e.g., "install jfrog CLI before fetching corporate toolchain recipe")
2. **Weak Dependencies**: Recipes reference dependencies without complete specs—error if missing (reference-only) or use fallback (weak)

Both integrate via iterative graph expansion/resolution with convergence.

## Implementation Status

- ✅ **Phase 0**: Phase Enum Unification (complete)
- ✅ **Phase 1**: Shared Fetch API (complete)
- ⚠️ **Phase 2**: Nested Source Dependencies (parsing complete, execution pending)
- ❌ **Phase 3**: Weak Dependency Resolution (not started)
- ❌ **Phase 4**: Integration (not started)
- ❌ **Phase 5**: Documentation & Polish (not started)

**Current State**: recipe_spec can parse `source.dependencies` with validation. Dynamic function lookup via `lookup_and_push_source_fetch()`. Execution in phase_recipe_fetch and recipe_fetch_context not yet implemented.

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

## Phase 2: Nested Source Dependencies (Recipe Fetch Prerequisites) - ⚠️ PARTIALLY COMPLETE

### Goal

Enable recipes to declare dependencies needed for fetching other recipes' Lua files.

### Status Summary

- ✅ Parsing: recipe_spec parses `source.dependencies` and validates constraints
- ❌ Execution: phase_recipe_fetch does not yet handle fetch_function sources
- ❌ Context API: No recipe_fetch_context implementation yet
- ❌ Functional tests: Only parsing unit tests exist

### Key Implementation Details

**recipe_spec changes** (src/recipe_spec.h/cpp):
- Added `fetch_function{}` empty struct variant to source_t
- Added `source_dependencies` field for nested prerequisites
- parse_source_table() validates and recursively parses dependencies
- lookup_and_push_source_fetch() dynamically retrieves functions (no caching)

**Design principle**: recipe_spec remains POD-like. No lua_State pointers cached. Functions looked up on-demand from owning recipe's lua_State.

### Remaining Work

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
2. Create `recipe_fetch_context` with fetch_dir/work_dir
3. Call custom fetch function via `lookup_and_push_source_fetch()`
4. Expect `work_dir/recipe.lua` as output, import to cache
5. Load cached recipe.lua, continue normal processing

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

- [ ] 2.3: Implement fetch dependency processing in `run_recipe_thread()`
  - Location: engine.cpp, after getting execution context, BEFORE phase loop
  - For each dep in `spec.source_dependencies`:
    - Cycle detection against `ctx.ancestor_chain` (same as regular deps)
    - `ensure_recipe(&fetch_dep_spec)` to get/create recipe node
    - Add to `r->dependencies` map with `needed_by = recipe_phase::recipe_fetch`
    - Build child ancestor chain: `ctx.ancestor_chain + r->spec->identity`
    - `start_recipe_thread(fetch_dep, completion, child_chain)`
  - Existing phase loop dependency wait logic handles blocking automatically
  - Unit test: extract cycle detection to `validate_fetch_dependency_cycle()`, test cases in isolation

- [ ] 2.4: Create `recipe_fetch_context` using shared fetch API
  - Compose from `fetch_context_common` (Phase 1)
  - Expose `ctx:fetch(source, sha256)`, `ctx:import_file(path, sha256)` to Lua
  - Provide `ctx:work_dir()`, `ctx:fetch_dir()` for path queries

- [ ] 2.5: Handle `fetch_function` variant in `phase_recipe_fetch`
  - Location: phase_recipe_fetch.cpp:173, add `else if (fetch_function*)` branch
  - Fetch deps guaranteed complete by this point (blocked in phase loop)
  - Create `recipe_fetch_context` with cache paths
  - Call custom fetch via `lookup_and_push_source_fetch()`, execute with `sol::protected_function`
  - Expect `work_dir/recipe.lua` output, import to `cache.recipes_dir / identity / recipe.lua`
  - Continue to line 177 equivalent (load cached recipe.lua, normal dependency processing)
  - Add trace events: `custom_fetch_start`, `custom_fetch_complete`

- [ ] 2.6: Cycle detection for fetch dependencies
  - Integrated into Task 2.3—same mechanism as regular deps (engine.cpp:250-268)
  - Extract to testable function: `validate_fetch_dependency_cycle(candidate, ancestor_chain)`
  - Unit tests: no cycle, direct cycle, self-loop, annotated error messages
  - Functional test: A fetch needs B, B fetch needs A → cycle error with full path

- [ ] 2.7: Functional tests for nested source dependencies
  - Test: Simple (A fetch needs B) - validates basic flow, blocking, completion
  - Test: Context API (custom fetch uses ctx:fetch(), ctx:import_file(), SHA256 verification)
  - Test: Multi-level nesting (A fetch needs B, B fetch needs C)
  - Test: Multiple fetch deps (A fetch needs [B, C] - parallel installation)
  - Test: Cycle detection (A fetch needs B, B fetch needs A → error with path)
  - All tests use TraceParser to verify event sequences, not just success/failure

**Testing strategy:**
- Unit tests: Parsing (done), cycle detection (extract to pure function)
- Functional tests: Threading, blocking, phase coordination, graph traversal
- Rationale: Engine threading/CV behavior not unit-testable without major refactoring; functional tests adequate (582 tests run in ~20s)

**Note**: Old `needed_by="recipe_fetch"` manifest syntax is deprecated. Use `source.dependencies` instead. No validation task needed—old syntax can be removed in separate cleanup.

---

## Phase 3: Weak Dependency Resolution - ❌ NOT STARTED

### Goal

Enable reference-only and weak dependencies with iterative graph resolution.

### Algorithm

**Strong Closure** (unchanged):
```
resolve_graph(manifest_roots):
  # Phase 1: Expand all strong dependencies recursively
  for root_spec in manifest_roots:
    root = ensure_recipe(root_spec)
    start_recipe_thread(root, recipe_phase::recipe_fetch)

  wait_for_resolution_phase()  # All recipe_fetch threads complete
```

**Weak Resolution** (new):
```
resolve_weak_references():
  # Collect all weak refs into tracking set
  unresolved = set()  # (recipe*, weak_reference*) pairs
  for recipe in graph.recipes:
    for weak_ref in recipe.weak_references:
      unresolved.add((recipe, weak_ref))

  # Convergence loop with stuck detection
  MAX_STUCK_ITERATIONS = 3  # Allow 3 iterations with no progress before error
  stuck_count = 0
  iteration = 0

  while True:
    iteration += 1

    if unresolved.empty():
      break  # Success: all resolved

    tui::info("Weak resolution iteration {}: {} unresolved refs", iteration, unresolved.size())

    # Queue weak fallbacks for unmatched refs
    queue = []
    for (recipe, ref) in unresolved:
      candidates = find_matches(ref.identity)  # Partial match: name/ns/rev

      if candidates.empty() and ref.has_weak_fallback:
        # No match, has fallback → queue for instantiation
        queue.append((recipe, ref, ref.weak_fallback_spec))

      # If candidates.empty() and no fallback: reference-only, check after convergence

    # Create + fetch weak fallback nodes
    newly_fetched = set()
    for (parent, ref, fallback_spec) in queue:
      ensure_result = cache.ensure_recipe(fallback_spec.identity)

      # Only fetch if newly created (lock present = needs installation)
      if ensure_result.lock:
        node = engine.ensure_recipe(fallback_spec)
        start_recipe_thread(node, recipe_phase::recipe_fetch)
        newly_fetched.add(node)

        # Add new node's weak refs to unresolved set
        for new_weak_ref in node.weak_references:
          unresolved.add((node, new_weak_ref))

    wait_for_resolution_phase()  # All fetches + strong transitives complete

    # Resolve weak refs against updated graph
    to_remove = []
    for (recipe, ref) in unresolved:
      candidates = find_matches(ref.identity)

      if candidates.size() == 1:
        # Unique match → resolve
        ref.resolved = candidates[0]
        recipe.dependencies[candidates[0].key.identity()] = candidates[0]
        to_remove.append((recipe, ref))

      elif candidates.size() > 1:
        # Ambiguous → error with candidate list
        error("Recipe '{}' reference '{}' is ambiguous. Candidates:\n  {}",
              recipe.identity, ref.identity,
              [c.key.canonical() for c in candidates])

      # candidates.empty(): leave in unresolved, retry next iteration

    # Remove resolved refs from tracking set
    for pair in to_remove:
      unresolved.remove(pair)

    tui::info("  Fetched {} new, resolved {} refs", newly_fetched.size(), to_remove.size())

    # Stuck detection: no progress means either done or error
    if newly_fetched.empty() and to_remove.empty():
      stuck_count += 1
      if stuck_count >= MAX_STUCK_ITERATIONS:
        # No progress for 3 iterations → likely missing reference-only deps
        break  # Exit to final validation (will error if unresolved remain)
    else:
      stuck_count = 0  # Reset on progress

  # Final validation: error on remaining reference-only refs
  for (recipe, ref) in unresolved:
    if not ref.has_weak_fallback:
      error("Reference-only '{}' in recipe '{}' not found after resolution",
            ref.identity, recipe.identity)
    else:
      # Weak ref still unresolved (shouldn't happen if convergence correct)
      error("Weak reference '{}' in '{}' unresolved (internal error)",
            ref.identity, recipe.identity)
```

**Partial Matching** (existing `recipe_key::matches()`):
- Query `"python"` matches `*.python@*` (any namespace, any revision)
- Query `"local.python"` matches `local.python@*` (any revision)
- Query `"python@r4"` matches `*.python@r4` (any namespace, specific revision)
- Query `"local.python@r4"` matches exactly (full identity, options ignored)

### Tasks

- [ ] 3.1: Extend `recipe_spec` for weak dependencies
  - Add `weak` field (std::optional<recipe_spec>) as fallback recipe
  - Parse: `{ recipe = "...", weak = {...} }` (weak) or `{ recipe = "..." }` (reference-only)
  - Validate: `weak` and `source` mutually exclusive (can't be both strong and weak)
  - Distinction: reference-only has `recipe` only; weak has `recipe` + `weak`; strong has `recipe` + `source`
  - Unit test: parse reference-only, weak, error on source+weak violation

- [ ] 3.2: Add `weak_references` to recipe struct
  - Struct: `weak_reference { identity, weak_fallback_spec, resolved, needed_by }`
  - Field: `std::vector<weak_reference> weak_references`
  - Initialized empty, populated during `phase_recipe_fetch`

- [ ] 3.3: Collect weak refs in `phase_recipe_fetch`
  - When parsing dependencies: check if `dep_spec` has no `source` field (weak or reference-only)
  - If no source: store in `recipe.weak_references` (don't instantiate node yet)
  - If has source: strong dep → `ensure_recipe()` + thread start (current behavior)
  - Unit test: recipe with weak refs populates `weak_references` vector

- [ ] 3.4: Implement `engine::resolve_weak_references()`
  - Collect all weak refs into `unresolved` set
  - Convergence loop with stuck detection (3 no-progress iterations)
  - Use existing `find_matches()` for candidate lookup
  - Track `newly_fetched` and `to_remove` for progress monitoring
  - Use `cache.ensure_recipe().lock` emptiness to detect newly created recipes
  - Log iteration count and progress at info level

- [ ] 3.5: Integrate into `engine::resolve_graph()`
  - After strong closure: call `resolve_weak_references()`
  - Before starting asset phases: ensure all refs resolved
  - Update comments explaining two-phase resolution

- [ ] 3.6: Error handling for weak resolution
  - Ambiguity: format candidate list with canonical keys
  - Reference-only not found: clear message, list unresolved identity
  - Convergence failure: include iteration count, suggest cycle or bug
  - Unit tests: verify error messages clear and actionable

- [ ] 3.7: Unit tests for weak resolution logic
  - Test: single weak ref, no match → fallback instantiated
  - Test: weak ref, match exists → fallback ignored
  - Test: reference-only, match exists → resolves
  - Test: reference-only, no match → error after convergence
  - Test: ambiguous match (two candidates) → error with list
  - Test: convergence after 2 iterations (cascading weak)
  - Test: Stuck detection (3 iterations no progress) → error with iteration count

- [ ] 3.8: Functional tests for weak dependencies
  - Test: Simple weak (A-weak→B{v=1}, no B in manifest → B{v=1} used)
  - Test: Strong overrides weak (manifest has B{v=2}, A-weak→B{v=1} → B{v=2} used)
  - Test: Ambiguity error (A-weak→C{v=1}, B-weak→C{v=2} → error with candidates)
  - Test: Transitive satisfaction (A-weak→B, B-strong→C, D-weak→C → D uses B's C)
  - Test: Coexistence (A-strong→C{v=1}, B-strong→C{v=2} → both build independently)
  - Test: Cascading weak (A-weak→B, B-weak→C, C-weak→D → 3 iterations)
  - Test: Reference-only satisfied by weak (A-ref→"B", C-weak→B → C's weak satisfies A)
  - Test: Partial matching (A-ref→"python" matches "local.python@r4")

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

# Weak Dependencies & Recipe Fetch Prerequisites Implementation Plan

## Overview

Two complementary features:

1. **Nested Source Dependencies**: Recipes declare prerequisites for fetching dependencies' recipes (e.g., "install jfrog CLI before fetching corporate toolchain recipe")
2. **Weak Dependencies**: Recipes reference dependencies without complete specs—error if missing (reference-only) or use fallback (weak)

Both integrate via iterative graph expansion/resolution with convergence.

## Implementation Status

- ✅ **Phase 0**: Phase Enum Unification
- ✅ **Phase 1**: Shared Fetch API
- ✅ **Phase 2**: Nested Source Dependencies
- ✅ **Phase 3**: Weak Dependency Resolution (parsing, collection, iterative resolution loop, batching/thread safety, ambiguity/missing reporting, progress detection, functional tests)
- ✅ **Phase 4**: Integration (weak refs across flows, doc/test updates)
- ℹ️ **Phase 5**: Documentation & Polish (only minor doc touch-ups remain)

**Current State**: Nested source dependencies and weak dependency resolution are fully implemented, threaded, and tested. The resolve loop lives in `resolve_graph` (while-progress wait/resolve), handles fallback growth and ambiguity aggregation, and functional coverage includes weak/strong chains, alternating refs, reference-only, and custom fetch cases. Integration tasks are reflected in code and tests; only minor polish/documentation remains.

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

**Status**: Implemented and exercised (parsing + execution)

**Supported Syntax**:
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

**Status**: Implemented (Phase 3)

**Syntax**:
```lua
dependencies = {
  { recipe = "python" },  -- Reference-only: error if not found

  {
    recipe = "python",  -- Weak: use fallback if not found
    weak = { source = "...", options = {...} }
  }
}
```

**Semantics**: Reference matches via partial identity (namespace/name/revision, options ignored). Weak provides fallback if no match exists after strong closure. Resolution is iterative/batched in `resolve_graph` with progress detection; ambiguities are collected and surfaced after full resolution.

### Integration

**Status**: Implemented (Phase 4)

Nested source dependencies support weak references:
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
_Status unchanged; completed._

## Phase 1: Shared Fetch API ✅ COMPLETE
_Status unchanged; completed._

## Phase 2: Nested Source Dependencies ✅ COMPLETE
_Parsing and execution implemented; prerequisites run to completion before custom fetch._

## Phase 3: Weak Dependency Resolution ✅ COMPLETE

- Weak/ref-only parsing, fallback wiring, and parent pointers in place.
- Iterative resolution loop in `resolve_graph`: `while (progress) { wait_for_resolution_phase(); resolve_weak_references(); }`.
- Progress detection fixed to handle multi-iteration growth (including chains where unresolved counts stay constant while new weak deps are fetched).
- Ambiguity/missing reporting aggregates after full resolution; needed_by respected; needed_by not allowed inside weak fallbacks.
- Thread safety via batched phase advancement before resolve passes.
- Functional coverage includes: weak vs strong chains, alternating weak/strong, reference-only success/failure, fallback growth, ambiguity listings, custom fetch interactions.

## Phase 4: Integration ✅ COMPLETE

- Weak refs usable in custom fetch `source.dependencies`; resolution honors partial matches via engine::match.
- No new threading constructs; existing engine APIs used for matching and graph growth.
- Progress/loop semantics updated in code and doc; needed_by propagation clarified (weak refs don’t carry inner needed_by).
- Functional test plan expanded (4.4) to cover all 4.x scenarios.

## Phase 5: Documentation & Polish (minor)

- Remaining work is minor doc maintenance; implementation and tests are in place.

---

## Testing

### Unit
- Shared fetch API helpers (Phase 1)
- Nested source dependency parsing/execution (Phase 2)
- Weak resolution behavior (Phase 3): fallback vs strong match, ambiguity, missing refs, multi-iteration/cascading, flat-progress detection, error aggregation

### Functional
- Weak deps: fallback used, strong overrides weak, reference-only resolved, ambiguity error with candidates, missing ref stuck detection, cascading weak chain, flat unresolved-count progress, alternating weak/strong reuse.
- Nested weak fetch deps: custom fetch with weak prerequisite uses fallback when absent; prefers existing strong helper over fallback when present.

### Validation
- Ambiguity and missing refs aggregated after full resolution
- Stuck detection when no progress with unresolved refs remaining

## Implementation Order (completed)

Phase 0 → Phase 1 → Phase 2 → Phase 3 → Phase 4; only minor doc polish (Phase 5) remains.
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

## Testing

### Unit
- Shared fetch API helpers (Phase 1)
- Nested source dependency parsing/execution (Phase 2)
- Weak resolution behavior (Phase 3): fallback vs strong match, ambiguity, missing refs, multi-iteration/cascading, flat-progress detection, error aggregation

### Functional
- Weak deps: fallback used, strong overrides weak, reference-only resolved, ambiguity error with candidates, missing ref stuck detection, cascading weak chain, flat unresolved-count progress, alternating weak/strong reuse.
- Nested weak fetch deps: custom fetch with weak prerequisite uses fallback when absent; prefers existing strong helper over fallback when present.

### Validation
- Ambiguity and missing refs aggregated after full resolution
- Stuck detection when no progress with unresolved refs remaining

## Implementation Order (completed)

Phase 0 → Phase 1 → Phase 2 → Phase 3 → Phase 4; only minor doc polish (Phase 5) remains.

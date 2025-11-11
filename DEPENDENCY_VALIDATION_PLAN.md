# Dependency Validation Implementation Plan

## Overview

Implement strict validation for all `ctx.asset()` calls to ensure recipes explicitly declare their dependencies. This prevents runtime errors from missing dependencies and enables better build graph analysis.

**Key Design Decisions:**
- Store dependencies in `recipe` struct (thread-safe runtime storage)
- Validate transitively (A→B→C allows A to call `ctx.asset("C")`)
- No caching for `default_shell` functions (evaluate fresh per `ctx.run()` call)
- Hard errors on validation failure
- Uniform validation for all contexts (build, fetch, install, default_shell)

---

## Implementation Phases

### Phase 1: Extract recipe struct to recipe.h
**Files to modify:**
- [x] Create `src/recipe.h`
- [x] Move `recipe` struct definition from `src/engine_phases/graph_state.h` to `src/recipe.h`
- [x] Add new field to recipe struct: `std::vector<std::string> declared_dependencies`
- [x] Update `src/engine_phases/graph_state.h` to `#include "recipe.h"`
- [x] Update all files that reference `recipe`:
  - [x] `src/engine_phases/create_recipe_nodes.cpp`
  - [x] `src/engine_phases/phase_recipe_fetch.cpp`
  - [x] `src/engine_phases/phase_check.cpp`
  - [x] `src/engine_phases/phase_fetch.cpp`
  - [x] `src/engine_phases/phase_stage.cpp`
  - [x] `src/engine_phases/phase_build.cpp`
  - [x] `src/engine_phases/phase_install.cpp`
  - [x] `src/engine_phases/phase_deploy.cpp`
  - [x] `src/engine_phases/phase_completion.cpp`
  - [x] `src/engine_phases/lua_ctx_bindings.cpp`

**Verification:**
- [x] Code compiles without errors
- [x] All existing tests pass

---

### Phase 2: Populate declared_dependencies
**File to modify:** `src/engine_phases/phase_recipe_fetch.cpp`

**Changes in `run_recipe_fetch_phase()`:**
- [x] After parsing `dep_configs` vector (around line 127), extract identity strings
- [x] Create `std::vector<std::string> dep_identities`
- [x] Loop through `dep_configs`: `dep_identities.push_back(dep_cfg.identity)`
- [x] Store in recipe struct via accessor: `accessor->second.declared_dependencies = std::move(dep_identities)`

**Verification:**
- [x] Code compiles
- [x] Add debug logging to verify dependencies are populated
- [x] Existing tests pass

---

### Phase 3: Implement transitive dependency checker
**File to create/modify:** `src/engine_phases/lua_ctx_bindings.cpp` (or new `src/engine_phases/validation.cpp`)

**New function to implement:**
```cpp
bool is_transitive_dependency(
  graph_state* state,
  std::string const& current_key,
  std::string const& target_identity
);
```

**Algorithm:**
- [x] Implement base case: if `current_key == target_identity`, return true
- [x] Implement cycle detection: `std::unordered_set<std::string> visited`
- [x] Recursive traversal:
  - [x] Look up current recipe in `state->recipes`
  - [x] Iterate through `recipe.declared_dependencies`
  - [x] For each dep, recursively call `is_transitive_dependency(state, dep_identity, target_identity)`
  - [x] Return true if any recursive call returns true
- [x] Return false if no path found

**Unit test file created:**
- [x] `src/lua_ctx_bindings_tests.cpp` with 11 test cases

**Verification:**
- [x] Unit test: direct dependency (A→B, A asks for B) returns true
- [x] Unit test: self-reference (A asks for A) returns true
- [x] Unit test: transitive dependency 2 levels (A→B→C, A asks for C) returns true
- [x] Unit test: transitive dependency 3 levels (A→B→C→D, A asks for D) returns true
- [x] Unit test: diamond dependency (A→B→D, A→C→D) returns true
- [x] Unit test: non-dependency (A→B, A asks for unrelated C) returns false
- [x] Unit test: circular dependencies (A→B→C→A) don't infinite loop, correctly find deps
- [x] Unit test: missing intermediate recipe (A→B→C, but B not in graph) returns false
- [x] Unit test: missing target recipe (A asks for C not in graph) returns false
- [x] Unit test: empty dependency list returns false
- [x] Unit test: multiple direct dependencies returns true
- [x] All unit tests pass (351 → 362 tests)

---

### Phase 4: Validate in ctx.asset()
**File to modify:** `src/engine_phases/lua_ctx_bindings.cpp`

**Changes in `lua_ctx_asset()` (around line 207):**
- [x] Before existing graph lookup, add validation check
- [x] Call `is_transitive_dependency(ctx->state, *ctx->key, requested_identity)`
- [x] If false, call `luaL_error()` with message:
  ```
  "ctx.asset: recipe '%s' does not declare dependency on '%s'"
  ```
- [x] Keep existing validation (recipe exists check, completed flag check)

**Test recipes created:**
- [x] `test_data/recipes/dep_val_lib.lua` - Base library
- [x] `test_data/recipes/dep_val_tool.lua` - Tool depending on lib
- [x] `test_data/recipes/dep_val_direct.lua` - Positive test: direct dependency
- [x] `test_data/recipes/dep_val_transitive.lua` - Positive test: transitive dependency
- [x] `test_data/recipes/dep_val_missing.lua` - Negative test: missing declaration

**Test file created:**
- [x] `functional_tests/test_dependency_validation.py` with 3 tests

**Verification:**
- [x] Test: recipe with proper dependency → succeeds
- [x] Test: recipe without dependency → fails with clear error message
- [x] Test: transitive dependency → succeeds
- [x] All existing functional tests pass (181 → 184 tests, all pass)

---

### Phase 5: Remove default_shell caching
**Files to modify:**
- [ ] `src/manifest.h`
- [ ] `src/manifest.cpp`

**Changes:**
- [ ] Delete field: `int default_shell_func_ref_` from manifest struct
- [ ] Delete field: `mutable std::once_flag resolve_flag_` from manifest struct
- [ ] Delete field: `mutable default_shell_cfg resolved_` from manifest struct
- [ ] Delete method: `resolve_default_shell()` entirely
- [ ] Update `parse_default_shell()` to just parse and store the global (no ref storage, no evaluation)
- [ ] Consider renaming or simplifying - may just need to store raw Lua global type

**Verification:**
- [ ] Code compiles
- [ ] Existing shell tests still pass (may need updates in Phase 6)

---

### Phase 6: Evaluate default_shell dynamically
**File to modify:** `src/engine_phases/lua_ctx_bindings.cpp`

**Changes in ctx.run() implementation (around lines 48-62):**
- [ ] Before calling `shell_run()`, evaluate shell configuration fresh
- [ ] Access manifest's `default_shell` global from Lua state
- [ ] If function: call with current `ctx` (ctx->key identifies current recipe)
- [ ] If table: parse with existing `shell_parse_custom_from_lua()`
- [ ] If constant: use shell_choice enum value
- [ ] Pass result to `shell_run()`
- [ ] No caching - evaluate every call

**Note:** This means `ctx.asset()` calls inside default_shell functions now have full recipe context and get validated like any other phase.

**Verification:**
- [ ] Test: default_shell as constant → works as before
- [ ] Test: default_shell as table → works as before
- [ ] Test: default_shell as function (no ctx.asset) → works
- [ ] Test: default_shell function calls ctx.asset(), recipe declares dep → succeeds
- [ ] Test: default_shell function calls ctx.asset(), recipe missing dep → fails with error

---

### Phase 7: Remove lua_ctx_asset_for_manifest
**File to modify:** `src/manifest.cpp`

**Changes:**
- [ ] Delete `lua_ctx_asset_for_manifest()` function entirely (lines ~28-55)
- [ ] This was only used for default_shell evaluation in manifest context
- [ ] Now default_shell functions use regular `lua_ctx_asset()` with full validation

**Verification:**
- [ ] Code compiles
- [ ] No references to deleted function remain
- [ ] All tests pass

---

## Testing Strategy

### Overview

Testing must cover algorithm correctness, race conditions, and realistic scenarios including `needed_by="recipe_fetch"` cases where recipes run before the full graph is populated.

**Key insight for race conditions:** If B→A→C (B depends on A, A depends on C), then when B's fetch/build runs, both A and C are guaranteed to exist in `state->recipes` with populated `declared_dependencies`. The engine's dependency resolution ensures this. If a recipe isn't in the graph when we look for it during validation, it's NOT a transitive dependency of the current recipe.

### Unit Tests (add to new `src/validation_tests.cpp`)

**Algorithm correctness:**
- [ ] Direct dependency (A→B, A asks for B) → returns true
- [ ] Transitive dependency 2 levels (A→B→C, A asks for C) → returns true
- [ ] Transitive dependency 3 levels (A→B→C→D, A asks for D) → returns true
- [ ] Non-dependency (A→B, A asks for C where C unrelated) → returns false
- [ ] Circular dependencies (A→B→C→A) → doesn't infinite loop, handles via visited set
- [ ] Diamond dependency (A→B→D, A→C→D, A asks for D) → returns true via both paths
- [ ] Self-reference (A asks for A) → returns true (base case)

**Race condition handling:**
- [ ] Missing intermediate recipe (A→B→C, but B not in graph) → returns false
- [ ] Missing target recipe (A asks for C, C not in graph) → returns false
- [ ] Empty dependency list (A has no deps, asks for B) → returns false

### Functional Tests (add to `functional_tests/test_dependency_validation.py`)

**Basic validation:**
- [ ] Recipe calls `ctx.asset()` on direct dependency (declared) → success
- [ ] Recipe calls `ctx.asset()` on direct dependency (not declared) → error with clear message
- [ ] Recipe calls `ctx.asset()` on transitive dependency 2 levels → success
- [ ] Recipe calls `ctx.asset()` on transitive dependency 3 levels → success
- [ ] Recipe calls `ctx.asset()` on unrelated recipe → error

**needed_by="recipe_fetch" scenarios (critical for race conditions):**
- [ ] Recipe with `needed_by="recipe_fetch"` calls `ctx.asset()` on direct dep in fetch phase → success
- [ ] Recipe with `needed_by="recipe_fetch"` calls `ctx.asset()` on transitive dep in fetch phase → success
- [ ] Recipe with `needed_by="recipe_fetch"` calls `ctx.asset()` on undeclared dep → error
- [ ] Verify transitive deps of `needed_by` recipe exist in graph when accessed

**Complex dependency graphs:**
- [ ] Diamond dependency (A→B→D, A→C→D, A calls ctx.asset("D")) → success
- [ ] Deep chain (A→B→C→D→E, A calls ctx.asset("E")) → success
- [ ] Multiple recipes sharing same base library, all validated in parallel → all succeed

**default_shell validation (Phase 6):**
- [ ] default_shell as constant (ENVY_SHELL.BASH) → works as before
- [ ] default_shell as table (custom shell config) → works as before
- [ ] default_shell function calls `ctx.asset()`, recipe declares dependency → success
- [ ] default_shell function calls `ctx.asset()`, recipe missing dependency → error
- [ ] default_shell function returns different shells per recipe based on ctx → works

**Stress tests (parallelism):**
- [ ] 10 recipes depending on same base library, build with ENVY_TEST_JOBS=8 → all succeed
- [ ] Deep transitive chain under parallel execution → validation doesn't race
- [ ] Multiple ctx.asset() calls in same recipe phase → all validated correctly

### Regression Testing
- [ ] Run full test suite after each phase
- [ ] Verify all existing test recipes have proper dependency declarations
- [ ] Update any recipes that fail validation with missing dependencies
- [ ] Ensure no performance degradation with typical recipe graphs

### Test Execution Commands

**Phase 3 (unit tests):**
```bash
./envy_unit_tests --test-case="is_transitive_dependency*"
```

**Phase 4 (functional tests):**
```bash
python3 -m functional_tests.test_dependency_validation
ENVY_TEST_JOBS=8 python3 -m functional_tests.test_dependency_validation
```

**Phase 6 (shell validation):**
```bash
python3 -m functional_tests.test_dependency_validation::TestShellValidation
```

**Full regression:**
```bash
./build.sh && python3 -m functional_tests
```

---

## Migration Notes

**Breaking Change:** Recipes that call `ctx.asset()` without declaring dependencies will now fail.

**Migration steps for existing recipes:**
1. Run tests with new validation enabled
2. Identify failing recipes from error messages
3. Add missing dependencies to recipe `dependencies` array
4. Verify transitive dependencies are captured

**Common patterns to check:**
- Build scripts that invoke tools via `ctx.asset("tool@version")`
- Custom fetch functions that use `ctx.asset("fetcher@version")`
- Install scripts that reference other deployed assets
- default_shell functions that use `ctx.asset("interpreter@version")`

---

## Performance Considerations

**Transitive dependency checking:** O(D * N) per `ctx.asset()` call, where:
- D = average dependency depth (typically 2-3 levels)
- N = average number of dependencies per recipe (typically 5-10)

For typical recipes: ~10-30 string comparisons per `ctx.asset()` call.

**default_shell re-evaluation:** Function called once per `ctx.run()` invocation per recipe. Typical cost: Lua function call + 1-2 string concatenations. Should be negligible compared to actual shell execution.

**Future optimization (if needed):**
- Cache `is_transitive_dependency()` results per recipe
- Cache `default_shell` result per recipe (with `ctx->key` as cache key)

---

## Open Questions / Future Work

- [ ] Should we add a "strict mode" flag to disable transitive dependency checking?
- [ ] Should we generate a dependency graph visualization tool?
- [ ] Should we implement a "lockfile" format for reproducible builds?
- [ ] Should we add static analysis to detect `ctx.asset()` calls without Lua execution?

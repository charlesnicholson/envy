# Implementation Work Tracker

## Current Focus: Build Phase Implementation

### Overview

Implementing the build phase to enable compilation and processing workflows. Build operates on stage_dir (working directory) and prepares artifacts for install phase. Common context utilities (run, asset, copy) implemented for all phases.

**Key Design Decisions:**
- **build = nil**: Skip build phase (stage_dir already prepared)
- **build = string**: Execute shell script with stage_dir as cwd
- **build = function(ctx)**: Programmatic build with full ctx API
- **Working directory**: All build happens in stage_dir (no separate build_dir)
- **Common utilities**: Shared Lua bindings in `lua_ctx_bindings.{cpp,h}`
- **Common API**: `lua_ctx_bindings_register_*(lua, ctx)` functions
- **All phases get**: run(), asset(), copy(), extract()
- **Phase-specific**: import() (fetch only), extract_all() (stage convenience)

**Context API Structure:**
```lua
ctx = {
  identity, options,
  fetch_dir, stage_dir, install_dir,  -- Phase-dependent which paths present

  -- Common functions (all phases):
  run(script, opts) -> {stdout, stderr},  -- Execute shell, log to TUI
  asset(identity) -> path,                -- Dependency path (validated)
  copy(src, dst),                         -- File/directory copy
  extract(filename, opts),                -- Extract single archive

  -- Phase-specific:
  import(src, dest, sha256),              -- Fetch only
  extract_all(opts),                      -- Stage convenience wrapper
}
```

---

## Phase 1: Foundation (Batch Fetch API)

**Goal:** Refactor fetch API to support concurrent batch downloads.

### Tasks:

**Completion Criteria:** All fetch operations use batch API with concurrent downloads, recipe SHA256 verification working, existing functionality preserved.

---

## Phase 2: Identity Validation ✅ COMPLETE

**Goal:** Ensure ALL recipes declare identity matching referrer's expectation.

**Rationale:** Identity validation catches typos, stale references, and copy-paste errors. It's a correctness check orthogonal to SHA256 verification (which is about network trust). ALL recipes benefit from identity validation, regardless of namespace.

### Tasks:

**Completion Criteria:** ALL recipes must declare matching identity field. No namespace exemptions.

**Results:** Identity validation fully implemented and tested. All 231 unit tests + 69 functional tests pass.

---

## Phase 3: Declarative Fetch ✅ COMPLETE

**Goal:** Support declarative fetch in recipes with concurrent downloads and verification.

### Tasks:

- [x] Update recipe_spec validation
  - [x] Local sources skip fetch phase (no files to fetch)
  - [ ] Git sources: defer implementation (TODO comment)

**Completion Criteria:** Recipes can declaratively fetch files with optional SHA256 verification, concurrent downloads work, per-file caching handles partial failures and corruption.

**Results:** Declarative fetch fully implemented with robust caching. All 5 declarative fetch tests + 4 caching tests pass.

---

## Phase 4: Programmatic Fetch (ctx.fetch() Lua API) ✅ COMPLETE

**Goal:** Expose `ctx.fetch()` and `ctx.commit_fetch()` Lua C functions for custom fetch logic.

### Tasks:

**Completion Criteria:** Custom fetch functions work, `ctx.fetch()` and `ctx.commit_fetch()` APIs complete, SHA256 verification separated from download, all programmatic fetch tests pass.

**Results:** Programmatic fetch fully implemented with two-phase download/commit model. All 98 functional tests pass (17 new programmatic, 1 new declarative string array).

---

## Phase 5: Stage Phase Implementation ✅ COMPLETE

**Goal:** Extract archives to staging area, prepare source tree for build phase.

**Implementation:** Stage phase supports declarative (table), imperative (function), and shell (string) forms. Default behavior extracts all archives from fetch_dir. Programmatic stage provides ctx API with extract/extract_all/run functions.

**Results:** Stage phase fully implemented with extraction support, shell execution, and ctx API. Handles strip_components option, non-archive files, and custom staging logic.

---

## Phase 6: Build Phase Implementation ✅ COMPLETE

**Goal:** Enable compilation and processing workflows. Build operates on stage_dir, prepares artifacts for install phase. Extract common context utilities (run, asset, copy) for use across all phases.

### Tasks

**Completion Criteria:** Build phase fully functional with nil/string/function forms. Common context utilities (run, asset, copy) available across all phases. Programmatic builds can access dependencies, execute shell commands, copy files. All tests pass.

**Results:** Build phase implemented with nil/string/function dispatch. Common Lua bindings refactored into lua_ctx_bindings.{cpp,h} with inheritance-based context design. All phases (fetch/stage/build/install) share common API: run(), asset(), copy(), move(), extract(). Phase-specific bindings remain separate (fetch()/commit_fetch() in fetch, extract_all() in stage, mark_install_complete() in install). Tested via integration with other phases.

## Status Legend

- [ ] Not started
- [x] Complete
- [~] In progress
- [!] Blocked

---

## Notes / Decisions

### Build Phase Architecture Finalized
- Build operates in stage_dir (no separate build_dir)
- Common Lua bindings in `lua_ctx_bindings.{cpp,h}`
- **Inheritance-based design:** `lua_ctx_common` exported in header, all phase contexts inherit from it
- Phase contexts: `fetch_context`, `stage_context`, `build_context` all inherit from `lua_ctx_common`
- Layout guarantee: inheritance ensures proper memory layout for safe casting to base type
- `lua_ctx_common` fields: `fetch_dir`, `run_dir` (default cwd for ctx.run()), `state`, `key`
- `run_dir` clarifies it's the phase-specific working directory (not lock->work_dir())
- **Lua-exposed fields:** `ctx.identity` (recipe's canonical identity, e.g. "local.python@r4"), `ctx.tmp_dir` (ephemeral working directory)
- **Note:** Manifest scripts don't get `ctx.identity` since manifests aren't recipes
- Registration API: `lua_ctx_bindings_register_*(lua_State*, context)`
- All phases get: run(), asset(), copy(), move(), extract()
- Phase-specific: fetch()/commit_fetch() (fetch), extract_all() (stage convenience)
- `ctx.run()` returns `{stdout, stderr}` while logging to TUI
- `ctx.asset()` validates dependency declaration and completion
- `ctx.copy()` auto-detects file vs directory
- `ctx.move()` provides efficient rename-based moves
- `ctx.extract()` moved from stage to common (single archive extraction)
- Shell choice uses platform default, not per-invocation auto-detect
- **Safety:** All Lua strings copied to std::string before popping to prevent use-after-free

### Deferred Work
- Git source support (use committish for now, implement clone later)
- Strict mode (enforce SHA256 requirements)
- BLAKE3 content verification (future verify command)
- Declarative build system support (cmake/make/meson table forms)
- `ctx.asset()` dependency validation - validate requested dependency is explicitly declared in current recipe and `needed_by` phase allows access. Requires storing evaluated dependencies in recipe struct during recipe_fetch phase. Note: default_shell function form (if implemented) would use ctx.asset() and have same limitation - currently no validation that referenced dependency exists or is completed.
- `ctx.copy()` / `ctx.move()` path validation - restrict destinations to writable directories (fetch/stage/install/tmp), sources to project root + ctx directories. Requires adding project_root to context and defining project root concept (likely: directory containing manifest).
- `ctx.run()` stdout/stderr separation - extend shell_run to use separate pipes for stdout and stderr. Currently both streams redirect to single pipe and merge in output. Separation requires either dual reader threads or async I/O multiplexing. Most build tools merge streams anyway (cmake/ninja/make). See future-enhancements.md for details.
- Shell pipe reading with pathological output - current implementation reads pipe synchronously on main thread while child runs, providing backpressure when pipe buffer fills. Works correctly for normal cases (tested with large outputs). Potential issues: (1) callback that blocks indefinitely can stall child, (2) single line >64KB without newline accumulates in memory. Could be addressed with background reader thread or async I/O if real-world cases emerge. See future-enhancements.md for analysis.

---

## Phase 7: Install Phase Implementation ✅ COMPLETE

**Completion Criteria:** Install phase fully functional with nil/string/function forms. Programmatic install functions can skip ctx.mark_install_complete() for packages without cached artifacts. Cache entries cleaned up when install_dir and fetch_dir are empty. Completion phase allows recipes without asset_path. All tests pass.

**Results:** Install phase implemented with all three forms. Programmatic packages supported—can skip mark_install_complete() to signal no cache usage. Cache purge logic detects empty install_dir + fetch_dir and removes entire entry. Completion phase accepts programmatic packages (result_hash="programmatic"). All 379 unit tests + 194 functional tests pass.

### Phase 8: User-Managed Packages (Double-Check Lock) ✅ COMPLETE

**Goal:** Enable packages that don't leave cache artifacts but need process coordination. User-managed packages use check function to determine install state; cache entries are ephemeral workspace (fully purged after installation).

**Key Design:**
- **Check verb presence** implies user-managed (no new recipe field needed)
- **Double-check lock pattern** in check phase: check → acquire lock → re-check → proceed or skip
- **Lock flag** (`mark_user_managed()`) signals destructor to purge entire `entry_dir`
- **Check XOR cache constraint:** User-managed recipes cannot call `mark_install_complete()` (runtime validation)
- **String check support:** `check = "command"` runs shell, returns true if exit 0
- **All phases allowed:** User-managed packages can use fetch/stage/build as ephemeral workspace

### Tasks

**Completion Criteria:** User-managed packages coordinate correctly across processes via double-check lock pattern. Check phase runs user check twice (pre-lock and post-lock) to detect races where install state changes. Cache entries fully purged (entire entry_dir deleted) for user-managed packages. String check form works (`check = "command"` runs shell). Validation errors when recipe has check verb but calls mark_install_complete(). All tests pass with no regressions.

**Results:** User-managed packages fully implemented with double-check lock pattern. Check phase refactored with separate helper functions (run_check_string, run_check_function, run_check_verb, recipe_has_check_verb). Lock destructor three-way branch: completed (success), user_managed (purge all), or cache_managed_failure (conditional purge). Install phase validates check XOR cache constraint and throws clear error if violated. Comprehensive unit tests (8 new phase_install_tests, 25 phase_check_tests) and functional tests (12 user_managed tests) verify behavior. String check support works with platform-appropriate shells. All 418 unit tests + 249 functional tests pass with no regressions.

---

### Phase 9: Deploy Phase Implementation (Future)
- Post-install actions (env setup, capability registration)
- Call user's `deploy(ctx)` function if defined
- Examples: PATH modification, library registration, service setup

---

### Phase 10: Verb Serialization and Validation (Future)

**Goal:** Parse and validate recipe verbs once at load time, reject invalid types (numbers), eliminate ad-hoc Lua VM manipulation across phases.

**Problem:** Currently each phase calls `lua_getglobal(lua, "verb_name")` and manually checks `lua_type()`. This leads to:
1. **Number coercion bug:** `lua_isstring()` returns true for numbers, so `check = 42` is accepted but meaningless
2. **Duplicate logic:** Every phase repeats the same type checking boilerplate
3. **Late error detection:** Invalid types discovered during phase execution, not recipe load
4. **Scattered validation:** No single source of truth for what types are valid for which verbs

**Analysis of Current Pattern:**
- **Most verbs** (stage, build, install): `nil`/`string`/`function` (string = shell command)
- **Fetch verb**: `nil`/`string`/`table`/`function` (string/table = declarative URL specs, NOT shell)
- **Check verb**: `nil`/`string`/`function` (string = shell with boolean result)
- **Common pattern**: All phases do string → shell execution except fetch (declarative)
- **Common pattern**: All phases call Lua functions with phase-specific context

**Proposed Design:**

```cpp
// lua_util.h - Add function placeholder type
struct lua_function_ref {};  // Indicates Lua function exists, must be called later

using lua_value = std::variant<
  std::monostate,
  bool,
  double,  // NEVER valid for verbs, but keep for general lua_value use
  std::string,
  std::vector<lua_value>,
  std::unordered_map<std::string, lua_value>,
  lua_function_ref  // NEW
>;

// Get global variable, serialize to lua_value, validate type
// Throws if global is a number (invalid for all verbs)
// Throws if global is a table and verb_name != "fetch" && verb_name != "stage"
std::optional<lua_value> lua_get_verb(lua_State *L, char const *verb_name);
```

```cpp
// recipe.h - Store parsed verbs in recipe struct
struct recipe {
  // ... existing fields ...

  // Parsed verb values (populated once during recipe loading)
  struct parsed_verbs {
    std::optional<lua_value> check;
    std::optional<lua_value> fetch;
    std::optional<lua_value> stage;
    std::optional<lua_value> build;
    std::optional<lua_value> install;
    std::optional<lua_value> deploy;
  } verbs;
};
```

**Benefits:**
- ✅ **Early validation:** Invalid types caught at recipe load time with clear errors
- ✅ **Single source of truth:** Parse once, use everywhere
- ✅ **Type safety:** No raw Lua stack manipulation in phases
- ✅ **Clear errors:** "check = 42 is invalid (expected string or function)"
- ✅ **Easier testing:** Mock recipe verbs without Lua VM
- ✅ **Better encapsulation:** Phases use structured data, not lua_State

**Possible Future Enhancement:** Unified verb executor (may be overkill):
```cpp
enum class verb_string_mode {
  shell_command,      // stage, build, install: exec and return
  shell_boolean,      // check: exec, return true if exit 0
  declarative_fetch   // fetch: parse as URL spec
};

// Unified executor handles both string and function dispatch
template<typename ResultType = void>
ResultType execute_verb(
    recipe *r,
    graph_state &state,
    lua_value const &verb,
    verb_string_mode mode,
    std::function<ResultType(lua_State*, lua_ctx_common const&)> function_handler
);
```

**Note:** Unified executor may not be worthwhile—each phase has unique context setup. Structured verb storage alone solves the main issues.

### Tasks (Deferred)

- [ ] 1. Extend lua_value with lua_function_ref
  - [ ] Add `struct lua_function_ref {}` to lua_util.h
  - [ ] Update `lua_value` variant to include `lua_function_ref`
  - [ ] Update `lua_value_to_string()` helper if needed for diagnostics

- [ ] 2. Implement lua_get_verb() with validation
  - [ ] Accept `lua_State*` and `char const *verb_name`
  - [ ] Call `lua_getglobal(L, verb_name)`
  - [ ] Switch on `lua_type(L, -1)`:
    - [ ] `LUA_TNIL` → return `std::nullopt`
    - [ ] `LUA_TFUNCTION` → return `lua_function_ref{}`
    - [ ] `LUA_TSTRING` → return `std::string` (copy before pop!)
    - [ ] `LUA_TTABLE` → if verb is fetch/stage, serialize; else throw
    - [ ] `LUA_TNUMBER` → throw with clear error: "{verb} = {number} is invalid (expected string or function)"
    - [ ] `LUA_TBOOLEAN` → throw: booleans invalid for all verbs
  - [ ] Pop value from stack before returning
  - [ ] Add comprehensive unit tests (20+ cases covering all types × all verbs)

- [ ] 3. Add parsed_verbs to recipe struct
  - [ ] Define `recipe::parsed_verbs` nested struct in recipe.h
  - [ ] Add optional<lua_value> fields for each verb
  - [ ] Initialize during recipe loading (find where lua_state is created)

- [ ] 4. Parse verbs during recipe loading
  - [ ] Find where recipe Lua state is created/loaded
  - [ ] After loading recipe script, call `lua_get_verb()` for each verb
  - [ ] Store results in `r->verbs.{check,fetch,stage,build,install,deploy}`
  - [ ] Handle errors: propagate exception with recipe identity context

- [ ] 5. Update phase_check to use parsed verbs
  - [ ] Replace `lua_getglobal(lua, "check")` with `r->verbs.check`
  - [ ] Replace `lua_isfunction()`/`lua_isstring()` with `std::holds_alternative<>()`
  - [ ] Update helper functions to take `lua_value const&` instead of poking VM
  - [ ] Verify tests still pass (behavior unchanged, just refactored)

- [ ] 6. Update remaining phases to use parsed verbs
  - [ ] phase_fetch: use `r->verbs.fetch`, handle table case
  - [ ] phase_stage: use `r->verbs.stage`
  - [ ] phase_build: use `r->verbs.build`
  - [ ] phase_install: use `r->verbs.install`
  - [ ] phase_deploy: use `r->verbs.deploy` (when implemented)
  - [ ] Remove all direct `lua_getglobal(lua, "verb")` calls from phases

- [ ] 7. Update validation in phase_recipe_fetch
  - [ ] Replace manual `lua_getglobal` checks with `r->verbs.*` access
  - [ ] Validation already happens in lua_get_verb(), simplify logic

- [ ] 8. Add tests for invalid verb types
  - [ ] Create test recipes with invalid verbs:
    - [ ] `check = 42` (number)
    - [ ] `build = true` (boolean)
    - [ ] `install = {}` (table, invalid for install)
  - [ ] Verify clear error messages at recipe load time
  - [ ] Test that errors include recipe identity for debugging

- [ ] 9. Documentation
  - [ ] Update architecture docs explaining verb parsing lifecycle
  - [ ] Document valid types for each verb in recipe format docs
  - [ ] Add rationale: early validation, type safety, single source of truth

**Completion Criteria:** All recipe verbs parsed and validated at load time. Numbers and other invalid types rejected with clear errors. Phases use structured verb data (`std::optional<lua_value>`) instead of manual Lua VM manipulation. All existing tests pass with no behavior changes. New tests verify invalid types are caught early.

**Notes:**
- Unified executor may be revisited later if pattern emerges
- Focus on verb storage first; execution pattern unification is optional
- Number rejection is the primary bug fix; other improvements are refactoring

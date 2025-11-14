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

## Phase 6: Build Phase Implementation

**Goal:** Enable compilation and processing workflows. Build operates on stage_dir, prepares artifacts for install phase. Extract common context utilities (run, asset, copy) for use across all phases.

### Tasks

- [x] 1. Create Common Lua Bindings
  - [x] Create `src/engine_phases/lua_ctx_bindings.{cpp,h}`
  - [x] Implement `lua_ctx_bindings_register_run(lua, context)`
    - [x] Internal: `lua_ctx_run()` - Execute shell, capture output, log to TUI
    - [x] Parse opts table for shell choice (bash/sh/cmd/powershell)
    - [x] Return `{stdout, stderr}` table (stderr empty until shell_run separation)
    - [x] Throw Lua error on non-zero exit
  - [x] Implement `lua_ctx_bindings_register_asset(lua, context)`
    - [x] Internal: `lua_ctx_asset()` - Dependency path access (simplified)
    - [x] Extract graph_state from context upvalue
    - [x] Look up dependency identity in graph_state.recipes
    - [x] Verify dependency completed flag is set
    - [x] Return asset_path
    - [x] NOTE: Full validation (dependency declaration + needed_by) deferred
  - [x] Implement `lua_ctx_bindings_register_copy(lua, context)`
    - [x] Internal: `lua_ctx_copy()` - File/directory copy helper
    - [x] Auto-detect file vs directory
    - [x] Handle recursive directory copy
    - [x] Error on filesystem exceptions
  - [x] Implement `lua_ctx_bindings_register_move(lua, context)`
    - [x] Internal: `lua_ctx_move()` - Safe move/rename
    - [x] Error if destination exists (no automatic overwrites)
  - [x] Implement `lua_ctx_bindings_register_extract(lua, context)`
    - [x] Internal: `lua_ctx_extract()` - Extract single archive
    - [x] Adapted from phase_stage.cpp
    - [x] Parse opts table for strip_components
    - [x] Use extract() from extract.h
  - [x] Add `lua_get_arg()` helper to lua_util.{h,cpp}
    - [x] Get argument at stack index, return optional<lua_value>
    - [x] Handle positive and negative indices
    - [x] Add 27 comprehensive unit tests

- [x] 2. Refactor Phase Fetch
  - [x] Remove `lua_ctx_run`, `lua_ctx_run_capture`, `lua_ctx_asset` from phase_fetch.cpp
  - [x] Reorder `fetch_context` struct to match `lua_ctx_common` layout
  - [x] Keep `lua_ctx_fetch` and `lua_ctx_commit_fetch` (phase-specific)
  - [x] Update `build_fetch_context_table()` to use `lua_ctx_bindings_register_*`
  - [x] Add include for `lua_ctx_bindings.h`

- [x] 3. Refactor Phase Stage
  - [x] Remove `lua_ctx_run` from phase_stage.cpp (now in lua_ctx_bindings)
  - [x] Remove `lua_ctx_extract` from phase_stage.cpp (now in lua_ctx_bindings)
  - [x] Keep `lua_ctx_extract_all` in phase_stage.cpp (convenience wrapper)
  - [x] Update `build_stage_context_table()` to use `lua_ctx_bindings_register_*`
  - [x] Add include for `lua_ctx_bindings.h`
  - [x] All 166 functional tests pass

- [x] 4. Implement Build Phase Logic
  - [x] Define `build_context` struct (fetch_dir, stage_dir, install_dir, state, key)
  - [x] Implement `build_build_context_table()` - Construct ctx with common bindings
    - [x] Set identity, options, fetch_dir, stage_dir, install_dir
    - [x] Call `lua_ctx_bindings_register_run(lua, ctx)`
    - [x] Call `lua_ctx_bindings_register_asset(lua, ctx)`
    - [x] Call `lua_ctx_bindings_register_copy(lua, ctx)`
    - [x] Call `lua_ctx_bindings_register_move(lua, ctx)`
    - [x] Call `lua_ctx_bindings_register_extract(lua, ctx)`
  - [x] Implement `run_programmatic_build()` - Handle function(ctx) case
  - [x] Implement `run_shell_build()` - Handle string script case
  - [x] Implement `run_build_phase()` - Main dispatcher (nil/string/function)
    - [x] Follow phase_stage.cpp pattern (type dispatch)
    - [x] Use stage_dir as working directory
    - [x] Provide fetch_dir, stage_dir, install_dir in ctx
  - [x] All 166 functional tests pass

- [x] 5. Update Build System
  - [x] Add `engine_phases/lua_ctx_bindings.cpp` to CMakeLists.txt

- [x] 6. Update Documentation
  - [x] Add declarative build systems section to `docs/future-enhancements.md`
    - [x] Table form for cmake/make/meson/ninja/cargo/autotools/meson
    - [x] Example: `build = { cmake = { args = {...} }, make = { jobs = 4 } }`
    - [x] Implementation considerations and trade-offs documented

- [ ] 7. Add Tests
  - [ ] Create `test_data/recipes/build_nil.lua` - No build phase
  - [ ] Create `test_data/recipes/build_string.lua` - Shell script build
  - [ ] Create `test_data/recipes/build_function.lua` - Programmatic with ctx.run()
  - [ ] Create `test_data/recipes/build_with_asset.lua` - Uses ctx.asset() for dependency
  - [ ] Create `test_data/recipes/build_with_copy.lua` - Uses ctx.copy()
  - [ ] Create `functional_tests/test_build.py` - Python test suite
    - [ ] Test build = nil (skip)
    - [ ] Test build = string (shell)
    - [ ] Test build = function (ctx API)
    - [ ] Test ctx.asset() dependency access
    - [ ] Test ctx.copy() file and directory
    - [ ] Test ctx.run() output capture

- [ ] 8. Verify All Tests Pass
  - [ ] Run `./build.sh` - All unit tests pass
  - [ ] Run functional tests - All tests pass
  - [ ] Verify Windows compatibility (separate task if needed)

**Completion Criteria:** Build phase fully functional with nil/string/function forms. Common context utilities (run, asset, copy) available across all phases. Programmatic builds can access dependencies, execute shell commands, copy files. All tests pass.

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

## Future Phases (Not Yet Started)

### Phase 7: Install Phase Implementation ✅ COMPLETE
- [x] 1. Phase dispatcher
  - [x] Implement install_phase entry mirroring stage/build (nil/string/function)
  - [x] String form: run shell in install_dir, mark complete on exit 0
  - [x] Function form: expose ctx helpers + `mark_install_complete()`
- [x] 2. Nil-form fallback
  - [x] Step 1: if install_dir contains anything, call mark_install_complete()
  - [x] Step 2: else if stage_dir has content, rename stage_dir -> install_dir, mark complete
  - [x] Step 3: else leave unmarked so cache purge path triggers
- [x] 3. Cache lock behavior
  - [x] Teach scoped_entry_lock dtor to detect "no install output" (flag false + empty install_dir + empty fetch_dir) and delete asset/, fetch/, install/, work/ before unlocking
  - [x] Preserve existing success/failure semantics for other cases
- [x] 4. Lua context bindings
  - [x] Add `lua_ctx_bindings_register_mark_install_complete()` (install-only)
  - [x] Ensure ctx table exposes install/stage/fetch dirs plus new API
- [x] 5. Tracing + errors
  - [x] Emit `tui::trace` coverage similar to other phases for script start/finish and fallback branches
  - [x] Propagate failures (non-zero exit / Lua error) as phase errors
- [x] 6. Completion phase
  - [x] Allow recipes without asset_path (programmatic packages)
  - [x] Set result_hash = "programmatic" for packages without cached artifacts
- [ ] 7. Tests
  - [ ] Unit tests for cache purge path + repeated mark_install_complete() calls
  - [ ] Functional tests (exhaustive):
    1. String install succeeds (populated install_dir → completes)
    2. String install fails (non-zero exit → no completion, fetch retained)
    3. Function install calls ctx.mark_install_complete() and succeeds
    4. Function install omits mark, leaves install_dir empty → programmatic package, cache purged
    5. Function install omits mark, populates install_dir → treated as nil case (fallback applies)
    6. Nil install: install_dir already populated → mark auto
    7. Nil install: install empty, stage populated → stage renamed to install, mark auto
    8. Nil install: both dirs empty → asset entry purged, run still succeeds
    9. 100% programmatic package: check phase looks at tmp file outside cache, install phase creates it
    10. 100% programmatic package: check succeeds (file exists) → skip install
    11. 100% programmatic package: check fails → install runs, cache entry cleaned up
    12. Programmatic package with fetch: fetches file, install skips mark → fetch_dir preserved
- [ ] 8. Docs
  - [ ] Update `docs/work.md` status once done; add any behavior notes to architecture docs if needed

**Completion Criteria:** Install phase fully functional with nil/string/function forms. Programmatic install functions can skip ctx.mark_install_complete() for packages without cached artifacts. Cache entries cleaned up when install_dir and fetch_dir are empty. Completion phase allows recipes without asset_path. All tests pass.

**Results:** Install phase implemented with all three forms. Programmatic packages supported—can skip mark_install_complete() to signal no cache usage. Cache purge logic detects empty install_dir + fetch_dir and removes entire entry. Completion phase accepts programmatic packages (result_hash="programmatic"). All 379 unit tests + 194 functional tests pass.

### Phase 8: User-Managed Packages (Double-Check Lock)

**Goal:** Enable packages that don't leave cache artifacts but need process coordination. User-managed packages use check function to determine install state; cache entries are ephemeral workspace (fully purged after installation).

**Key Design:**
- **Check verb presence** implies user-managed (no new recipe field needed)
- **Double-check lock pattern** in check phase: check → acquire lock → re-check → proceed or skip
- **Lock flag** (`mark_user_managed()`) signals destructor to purge entire `entry_dir`
- **Check XOR cache constraint:** User-managed recipes cannot call `mark_install_complete()` (runtime validation)
- **String check support:** `check = "command"` runs shell, returns true if exit 0
- **All phases allowed:** User-managed packages can use fetch/stage/build as ephemeral workspace

### Tasks

- [x] 1. Cache Lock - Add user-managed flag
  - [x] Add `bool user_managed_ = false;` private member to `scoped_entry_lock` class
  - [x] Add `void mark_user_managed()` public method to set flag
  - [x] Modify destructor three-way branch logic:
    - [x] Success path: `if (completed_)` - existing logic unchanged (rename install→asset, touch envy-complete)
    - [x] User-managed path: `else if (user_managed_)` - call `remove_all_noexcept(entry_dir_)` to purge entire entry
    - [x] Cache-managed failure path: `else` - existing conditional purge logic (check install_empty && fetch_empty)
  - [x] Verify lock file release and deletion attempt (with error_code to handle held locks)

- [x] 2. Check Phase - Implement double-check lock pattern
  - [x] Create helper: `run_check_string(recipe&, string_view check_cmd)`
    - [x] Use shell execution with manifest's `default_shell` setting
    - [x] Reuse shell selection logic from `ctx.run()` implementation
    - [x] Return true if `exit_code == 0`, false otherwise
    - [x] Handle shell execution errors with clear messages
  - [x] Create helper: `run_check_function(recipe&, lua_State*)`
    - [x] Extract from existing check phase Lua function call logic
    - [x] Call user's Lua check function with empty ctx table
    - [x] Return boolean result from `lua_toboolean()`
    - [x] Propagate Lua errors with recipe context
  - [x] Create helper: `run_check_verb(recipe&)`
    - [x] Dispatch to `run_check_string()` or `run_check_function()` based on recipe verb type
    - [x] Handle missing check verb (return false to maintain existing behavior)
    - [x] Throw errors with clear diagnostics on execution failure
  - [x] Create helper: `recipe_has_check_verb(recipe&, lua_State*)` for detection
  - [x] Modify main `run_check_phase()` logic:
    - [x] Detect if recipe has check verb using helper function
    - [x] **First check (pre-lock):** Call `run_check_verb(r)` to get initial state
    - [x] If check returns true: return early (no lock needed, phases skip)
    - [x] If check returns false:
      - [x] Call `state.cache_.ensure_asset(...)` to acquire lock (blocks if contended)
      - [x] If lock acquired: call `lock->mark_user_managed()` to signal full purge
      - [x] **Second check (post-lock):** Call `run_check_verb(r)` again to detect races
      - [x] If still needed (check=false): move lock to `r->lock`, phases will execute
      - [x] If no longer needed (check=true): let lock destructor run, purging entry, phases skip
    - [x] Preserve existing cache-managed behavior when no check verb present
  - [x] Add comprehensive `tui::trace` logging:
    - [x] "phase check: running user check (pre-lock)"
    - [x] "phase check: user check returned {true|false}"
    - [x] "phase check: acquiring lock for user-managed package"
    - [x] "phase check: re-running user check (post-lock)"
    - [x] "phase check: re-check returned {true|false}"
    - [x] "phase check: releasing lock" (when re-check succeeds)
  - [x] Remove anonymous namespace for testability (helpers have external linkage)
  - [x] Create comprehensive unit tests (`src/engine_phases/phase_check_tests.cpp`)
    - [x] 25 unit tests covering all helper functions
    - [x] Test string checks with various exit codes
    - [x] Test function checks with various return values (bool, nil, truthy)
    - [x] Test dispatch logic (string vs function vs missing)
    - [x] Test error handling (invalid commands, Lua errors)
    - [x] Use extern declarations to test helpers without header pollution
    - [x] All 411 unit tests pass (1543 assertions)

- [x] 3. Recipe Helper - Check verb detection
  - [x] Create `recipe_has_check_verb(recipe*, lua_State*)` helper function
    - [x] Return true if recipe has check verb defined (string or function)
    - [x] Return false if check verb absent
    - [x] Location: `src/engine_phases/phase_check.cpp` (external linkage, no header)
  - [x] Use consistently in check phase (install phase validation deferred to task 4)

- [ ] 4. Install Phase - Add validation for check XOR cache
  - [ ] After user install function/string completes successfully:
    - [ ] Check if recipe has check verb: `if (recipe_has_check_verb(r) && lock->completed())`
    - [ ] If true, throw runtime error with clear message: [ ] "Recipe {identity} has check verb (user-managed) but called mark_install_complete()"
      - [ ] "User-managed recipes must not populate cache. Remove check verb or remove mark_install_complete() call."
  - [ ] Update phase comments explaining user-managed vs cache-managed distinction
  - [ ] Document that user-managed packages CAN use fetch/stage/build (workspace gets purged regardless)

- [ ] 5. Tests - User-managed package scenarios
  - [ ] Create test recipes in `test_data/recipes/`:
    - [ ] `user_managed_simple.lua` - function check + install verb, no mark_install_complete
    - [ ] `user_managed_string_check.lua` - string check form: `check = "python3 --version"`
    - [ ] `user_managed_with_fetch.lua` - has fetch/stage/build verbs, installs system tool, cache purges all dirs
    - [ ] `user_managed_invalid.lua` - has check verb + calls mark_install_complete (should error in install phase)
  - [ ] Create comprehensive test suite `functional_tests/test_user_managed.py`:
    - [ ] Test: First run with check=false acquires lock, runs install, cache entry fully purged afterward
    - [ ] Test: Second run with check=true skips all phases, no lock acquired, immediate return
    - [ ] Test: Concurrent processes coordinate via lock, no duplicate work performed
    - [ ] Test: Race condition - process A installs while B waits, B's re-check returns true, B releases lock
    - [ ] Test: String check form - exit code 0 returns true, non-zero returns false
    - [ ] Test: User-managed with fetch verb - fetch_dir gets populated during install, fully purged at end
    - [ ] Test: Validation error - recipe with check + mark_install_complete throws clear error message
    - [ ] Test: Cache state after install - verify entry_dir fully deleted (no directories remain)
    - [ ] Test: Multiple runs - verify cache entry created/purged/created on each install cycle
  - [ ] Review existing "programmatic" package tests for compatibility
  - [ ] Update any test recipes that accidentally violate check XOR cache constraint
  - [ ] Verify no regressions in existing test suite (all tests still pass)

- [ ] 6. Documentation - Explain user-managed packages
  - [ ] Update `docs/architecture.md`:
    - [ ] Add new section: "User-Managed vs Cache-Managed Packages"
    - [ ] Define user-managed: check verb present, artifacts managed by user (system installs, env changes)
    - [ ] Define cache-managed: no check verb, artifacts in cache, envy-complete marker
    - [ ] Document double-check lock pattern and why it's needed (race where check state changes)
    - [ ] Show examples: system package wrappers (brew install, apt-get, python system checks)
    - [ ] Explain constraint: check XOR cache (recipes must choose one approach, not both)
    - [ ] Document that user-managed can use fetch/stage/build (ephemeral workspace)
  - [ ] Update `docs/cache.md`:
    - [ ] Add user-managed package lifecycle to existing cache diagrams
    - [ ] Explain ephemeral entries: created during install, fully purged at completion
    - [ ] Document lock coordination without persistent cache artifacts
    - [ ] Show entry_dir contents during install vs after completion (empty)
  - [ ] Update `docs/work.md`:
    - [ ] Mark Phase 8 as complete when finished
    - [ ] Add notes section documenting future enhancements:
      - [ ] In-memory check result cache (avoid expensive re-checks across DAG execution)
      - [ ] `asset` verb for custom path resolution (user-managed packages can provide paths)
  - [ ] Update `docs/commands.md` if `asset` command behavior with user-managed packages is documented

- [ ] 7. Verify All Tests Pass
  - [ ] Run `./build.sh` - verify all unit tests pass (no regressions)
  - [ ] Run functional tests - verify all tests pass including new user-managed suite
  - [ ] Specifically verify no regressions in existing programmatic package behavior
  - [ ] Test concurrent execution scenarios (multiple processes on same user-managed package)
  - [ ] Verify cache state after failures (lock released, entry purged correctly)

**Completion Criteria:** User-managed packages coordinate correctly across processes via double-check lock pattern. Check phase runs user check twice (pre-lock and post-lock) to detect races where install state changes. Cache entries fully purged (entire entry_dir deleted) for user-managed packages. String check form works (`check = "command"` runs shell). Validation errors when recipe has check verb but calls mark_install_complete(). All tests pass with no regressions.

**Results:** (To be filled in when phase completes)

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

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
- `ctx.asset()` dependency validation - validate requested dependency is explicitly declared in current recipe and `needed_by` phase allows access. Requires storing evaluated dependencies in recipe struct during recipe_fetch phase.
- `ctx.copy()` / `ctx.move()` path validation - restrict destinations to writable directories (fetch/stage/install/tmp), sources to project root + ctx directories. Requires adding project_root to context and defining project root concept (likely: directory containing manifest).
- `ctx.run()` stdout/stderr separation - extend shell_run to use separate pipes for stdout and stderr, requiring select()/poll() on POSIX and proper handle management on Windows. Currently returns combined output in stdout field, stderr is always empty.

---

## Future Phases (Not Yet Started)

### Phase 7: Install Phase Implementation
- Analyze current stub implementation in `phase_install.cpp`
- Propose correct behavior: install pulls artifacts from stage_dir to install_dir
- Fallback: if no install() function and install_dir has content, mark complete
- Classic example: `ctx.run("make install")` after build phase

### Phase 8: Deploy Phase Implementation
- Post-install actions (env setup, capability registration)
- Call user's `deploy(ctx)` function if defined
- Examples: PATH modification, library registration, service setup

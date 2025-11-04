# Implementation Work Tracker

## Current Focus: Fetch Phase Implementation

### Overview

Implementing the fetch phase with concurrent downloads, optional SHA256 verification, identity validation, and both declarative and imperative fetch support.

**Key Decisions Documented In:**
- `docs/recipe_resolution.md` - Fetch architecture, identity validation, ctx API
- `docs/architecture.md` - Recipe verbs, fetch syntax, verification model

**Trust Model:**
- **Identity validation**: ALL recipes must declare matching identity field (no exemptions)
- **SHA256 verification**: Optional (permissive by default)
  - `local.*` recipes: Never require SHA256 (files are local/trusted)
  - Non-`local.*` recipes: SHA256 optional now, required in future "strict mode"

---

## Phase 1: Foundation (fetch.h Refactor + SHA256)

**Goal:** Refactor fetch API to support batch downloads with optional SHA256 verification.

### Tasks:

- [ ] Refactor `fetch()` signature
  - [ ] Change from `fetch_result fetch(fetch_request const &request)`
  - [ ] To `fetch_batch_result fetch(vector<fetch_request> const &requests)`
  - [ ] Define `fetch_batch_result` struct with successes/failures vectors

- [ ] Add SHA256 support
  - [ ] Add `optional<string> sha256` field to `fetch_request` struct
  - [ ] Implement SHA256 computation after download (use existing hash utilities)
  - [ ] If sha256 provided, verify matches; add to failures if mismatch
  - [ ] If sha256 absent, skip verification (permissive mode)

- [ ] Implement parallel execution
  - [ ] Use TBB `task_group` for concurrent downloads
  - [ ] One task per fetch_request
  - [ ] Each task uses existing single-file fetch logic
  - [ ] Aggregate successes/failures after all tasks complete

- [ ] Update existing callers
  - [ ] Find all `fetch()` call sites
  - [ ] Wrap single requests: `fetch({request})` → vector
  - [ ] Verify all callers still work

- [ ] Testing
  - [ ] Existing fetch tests still pass
  - [ ] Add test: batch download (3+ files)
  - [ ] Add test: SHA256 verification success
  - [ ] Add test: SHA256 verification failure (mismatch)
  - [ ] Add test: mixed (some with SHA256, some without)

**Completion Criteria:** All fetch operations use batch API, SHA256 verified when provided, existing functionality preserved.

---

## Phase 2: Identity Validation

**Goal:** Ensure ALL recipes declare identity matching referrer's expectation.

**Rationale:** Identity validation catches typos, stale references, and copy-paste errors. It's a correctness check orthogonal to SHA256 verification (which is about network trust). ALL recipes benefit from identity validation, regardless of namespace.

### Tasks:

- [ ] Implement validation in `fetch_recipe_and_spawn_dependencies()`
  - [ ] After `lua_run_file()`, before `validate_phases()`
  - [ ] `lua_getglobal(lua, "identity")`
  - [ ] Verify is string and matches `spec.identity`
  - [ ] Throw error if missing: "Recipe must declare 'identity' field: {spec.identity}"
  - [ ] Throw error if mismatch: "Identity mismatch: expected '{spec.identity}' but recipe declares '{declared_identity}'"
  - [ ] **No namespace exemptions** - applies to ALL recipes including `local.*`

- [ ] Update existing test recipes
  - [ ] Add `identity = "..."` to all test recipe files that are missing it
  - [ ] Both `local.*` and non-local test recipes

- [ ] Testing
  - [ ] Test: recipe with correct identity succeeds
  - [ ] Test: recipe with wrong identity fails with clear error
  - [ ] Test: recipe missing identity field fails
  - [ ] Test: `local.*` recipe with correct identity succeeds
  - [ ] Test: `local.*` recipe with wrong identity fails (no exemption)
  - [ ] Verify all existing test recipes work after adding identity fields

**Completion Criteria:** ALL recipes must declare matching identity field. No namespace exemptions.

---

## Phase 3: Declarative Fetch

**Goal:** Support declarative fetch in recipes with concurrent downloads and verification.

### Tasks:

- [ ] Extend `recipe_spec` parsing
  - [ ] Detect fetch field type (string, single table, array of tables)
  - [ ] Parse string: `fetch = "url"` → no sha256
  - [ ] Parse single: `fetch = {url="...", sha256="..."}` → optional sha256
  - [ ] Parse batch: `fetch = {{url="..."}, {...}}` → multiple fetch_requests
  - [ ] Store parsed fetch_requests in new `recipe_spec` field
  - [ ] Handle filename collisions: detect duplicate basenames, error early

- [ ] Implement `run_fetch_phase()` for declarative fetch
  - [ ] Check if we have lock (cache miss path) - if not, return early
  - [ ] Skip if `spec.has_fetch_function()` (defer to Phase 4)
  - [ ] Extract parsed fetch_requests from recipe_spec
  - [ ] Set destinations to temp directory
  - [ ] Call `fetch(requests)`
  - [ ] If any failures, throw with aggregate error message
  - [ ] Move all downloaded files from temp to `lock->fetch_dir()`
  - [ ] Call `lock->mark_fetch_complete()`

- [ ] Update recipe_spec validation
  - [ ] Local sources should skip fetch phase (no files to fetch)
  - [ ] Git sources: defer implementation (TODO comment)

- [ ] Testing
  - [ ] Test: recipe with `fetch = "url"` (string) downloads without verification
  - [ ] Test: recipe with `fetch = {url="...", sha256="..."}` verifies
  - [ ] Test: recipe with batch fetch downloads concurrently
  - [ ] Test: SHA256 mismatch fails with clear error
  - [ ] Test: filename collision detected and errors early
  - [ ] Test: local source skips fetch phase
  - [ ] Verify `test_fetch_function_basic` and `test_fetch_function_with_dependency` still pass (even if stubs)

**Completion Criteria:** Recipes can declaratively fetch files with optional SHA256 verification, concurrent downloads work.

---

## Phase 4: Imperative Fetch (ctx.fetch() Lua API)

**Goal:** Expose `ctx.fetch()` Lua C function for custom fetch logic.

### Tasks:

- [ ] Implement `ctx.fetch()` Lua C function
  - [ ] Detect polymorphic input: single table vs array of tables
  - [ ] Parse table fields: `url` (required), `sha256` (optional)
  - [ ] Build `vector<fetch_request>` with destinations in `ctx.tmp`
  - [ ] Generate unique filenames (basename of URL)
  - [ ] Call `fetch(requests)`
  - [ ] If any failures, throw Lua error with aggregate message
  - [ ] Return to Lua: single basename (string) or array of basenames (table)

- [ ] Implement fetch phase context
  - [ ] Create context table in `run_fetch_phase()`
  - [ ] Add `ctx.identity` (string)
  - [ ] Add `ctx.options` (table, always present even if empty)
  - [ ] Add `ctx.tmp` (string, temp directory path)
  - [ ] Add `ctx.fetch` (C function)
  - [ ] Add `ctx.asset` (function to get dependency paths)
  - [ ] Add `ctx.run` and `ctx.run_capture` (subprocess execution)

- [ ] Update `run_fetch_phase()` for fetch functions
  - [ ] If `spec.has_fetch_function()`:
    - [ ] Create temp directory for `ctx.tmp`
    - [ ] Build context table
    - [ ] Call `lua_getglobal(lua, "fetch")`
    - [ ] Push context table
    - [ ] Call `lua_pcall(lua, 1, 0, 0)`
    - [ ] Handle errors, clean up temp
    - [ ] Move files from `ctx.tmp` to `lock->fetch_dir()`
    - [ ] Call `lock->mark_fetch_complete()`

- [ ] Testing
  - [ ] Test: `ctx.fetch({url="..."})` single file downloads and returns basename
  - [ ] Test: `ctx.fetch({{url="..."}, {...}})` batch downloads concurrently
  - [ ] Test: SHA256 verification in `ctx.fetch()` works
  - [ ] Test: SHA256 mismatch in `ctx.fetch()` throws Lua error
  - [ ] Test: `ctx.identity` and `ctx.options` accessible in fetch function
  - [ ] Test: `ctx.asset()` returns dependency paths
  - [ ] Verify `test_fetch_function_basic` now fully functional (not stub)
  - [ ] Verify `test_fetch_function_with_dependency` fully functional

**Completion Criteria:** Custom fetch functions work, `ctx.fetch()` API complete, all imperative fetch tests pass.

---

## Status Legend

- [ ] Not started
- [x] Complete
- [~] In progress
- [!] Blocked

---

## Notes / Decisions

### 2024-01-XX: Fetch Phase Architecture Finalized
- Polymorphic `ctx.fetch()` API (single or batch)
- Atomic commit: all-or-nothing downloads
- SHA256 optional (permissive mode)
- Identity validation required for non-`local.*`
- Concurrent downloads via TBB task_group
- Try-all error collection (show all failures)

### Deferred Work
- Git source support (use committish for now, implement clone later)
- Archive extraction (that's stage phase, not fetch)
- Strict mode (enforce SHA256 requirements)
- BLAKE3 content verification (future verify command)

---

## Future Phases (Not Yet Started)

### Phase 5: Stage Phase Implementation
- Extract archives to `lock->stage_dir()`
- Call user's `stage(ctx)` function if defined
- Default behavior: extract all archives from fetch_dir

### Phase 6: Build Phase Implementation
- Call user's `build(ctx)` function
- Provide `ctx.stage_dir`, `ctx.install_dir`, dependency access

### Phase 7: Deploy Phase Implementation
- Call user's `deploy(ctx)` function
- Post-install actions (env setup, capability registration)

### Phase 8: Context API Completion
- Implement full `ctx` API for all phases (stage/build/install/deploy)
- Add `ctx.fetch_dir` for later phases
- Add `ctx.stage_dir`, `ctx.install_dir` where appropriate
- Add helper functions (extract, copy, symlink, etc.)

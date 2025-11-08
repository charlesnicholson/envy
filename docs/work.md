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

## Phase 1: Foundation (Batch Fetch API)

**Goal:** Refactor fetch API to support concurrent batch downloads.

### Tasks:

- [x] Refactor `fetch()` to batch API
  - [x] Change from `fetch_result fetch(fetch_request const &request)`
  - [x] To `vector<fetch_result_t> fetch(vector<fetch_request> const &requests)`
  - [x] Use `variant<fetch_result, string>` for success/error per item

- [x] Implement parallel execution
  - [x] Use TBB `task_group` for concurrent downloads
  - [x] One task per fetch_request
  - [x] Rename existing fetch logic to `fetch_single()` helper
  - [x] Return 1:1 result vector (no mutex needed - each task owns its index)

- [x] Update existing callers
  - [x] Find all `fetch()` call sites
  - [x] Wrap single requests: `fetch({request})` → vector
  - [x] Handle variant results with error checking
  - [x] Verify all callers still work

- [x] Testing
  - [x] Existing fetch tests still pass (updated to use batch API)
  - [x] Add test: batch download (3+ files)

- [x] Add SHA256 support (recipe verification)
  - [x] Implemented `sha256_verify()` helper in sha256.h/cpp (hex-to-bytes comparison)
  - [x] SHA256 verification happens in engine after recipe fetch, before loading
  - [x] Permissive mode: SHA256 optional in manifests (empty string = no verification)
  - [x] Mismatch is always fatal with detailed error message
  - [x] Unit tests: 6 tests covering hex parsing, case-insensitivity, validation, errors
  - [x] Functional tests: 2 tests for correct/incorrect SHA256 verification
  - [ ] Asset fetch SHA256 verification deferred to Phase 3/4

**Completion Criteria:** All fetch operations use batch API with concurrent downloads, recipe SHA256 verification working, existing functionality preserved.

---

## Phase 2: Identity Validation ✅ COMPLETE

**Goal:** Ensure ALL recipes declare identity matching referrer's expectation.

**Rationale:** Identity validation catches typos, stale references, and copy-paste errors. It's a correctness check orthogonal to SHA256 verification (which is about network trust). ALL recipes benefit from identity validation, regardless of namespace.

### Tasks:

- [x] Implement validation in `fetch_recipe_and_spawn_dependencies()`
  - [x] After `lua_run_file()`, before `validate_phases()` (src/engine.cpp:361-376)
  - [x] `lua_getglobal(lua, "identity")`
  - [x] Verify is string and matches `spec.identity`
  - [x] Throw error if missing: "Recipe must declare 'identity' field: {spec.identity}"
  - [x] Throw error if mismatch: "Identity mismatch: expected '{spec.identity}' but recipe declares '{declared_identity}'"
  - [x] **No namespace exemptions** - applies to ALL recipes including `local.*`

- [x] Update existing test recipes
  - [x] Add `identity = "..."` to all 47 test recipe files
  - [x] Both `local.*` and non-local test recipes
  - [x] Converted all recipes from `@1.0.0` to `@v1` versioning

- [x] Testing
  - [x] Test: recipe with correct identity succeeds (test_identity_validation_correct)
  - [x] Test: recipe with wrong identity fails with clear error (test_identity_validation_mismatch)
  - [x] Test: recipe missing identity field fails (test_identity_validation_missing)
  - [x] Test: recipe with wrong type fails (test_identity_validation_wrong_type)
  - [x] Test: `local.*` recipe with correct identity succeeds (all existing tests)
  - [x] Test: `local.*` recipe without identity fails (test_identity_validation_local_recipe - no exemption)
  - [x] Verify all existing test recipes work after adding identity fields (231 unit + 69 functional tests pass)

**Completion Criteria:** ALL recipes must declare matching identity field. No namespace exemptions.

**Results:** Identity validation fully implemented and tested. All 231 unit tests + 69 functional tests pass.

---

## Phase 3: Declarative Fetch ✅ COMPLETE

**Goal:** Support declarative fetch in recipes with concurrent downloads and verification.

### Tasks:

- [x] Extend `recipe_spec` parsing
  - [x] Detect fetch field type (string, single table, array of tables)
  - [x] Parse string: `fetch = "url"` → no sha256
  - [x] Parse single: `fetch = {url="...", sha256="..."}` → optional sha256
  - [x] Parse batch: `fetch = {{url="..."}, {...}}` → multiple fetch_requests
  - [x] Store parsed fetch_requests in new `recipe_spec` field
  - [x] Handle filename collisions: detect duplicate basenames, error early

- [x] Implement `run_fetch_phase()` for declarative fetch
  - [x] Check if we have lock (cache miss path) - if not, return early
  - [x] Skip if `spec.has_fetch_function()` (defer to Phase 4)
  - [x] Extract parsed fetch_requests from recipe_spec
  - [x] Set destinations to `lock->fetch_dir()`
  - [x] Call `fetch(requests)`
  - [x] If any failures, throw with aggregate error message
  - [x] Implement per-file caching with SHA256 verification
  - [x] Call `lock->mark_fetch_complete()` after successful completion

- [x] Implement per-file caching and recovery
  - [x] Cache individual files even on partial failure
  - [x] Verify cached files by SHA256 on subsequent runs
  - [x] Detect corrupted cache files and re-download
  - [x] Trust files with SHA256, re-download files without SHA256
  - [x] Delete fetch/ directory after successful asset install

- [x] Update recipe_spec validation
  - [x] Local sources skip fetch phase (no files to fetch)
  - [ ] Git sources: defer implementation (TODO comment)

- [x] Testing (5 declarative fetch tests + 4 caching tests)
  - [x] Test: recipe with `fetch = "url"` (string) downloads without verification
  - [x] Test: recipe with `fetch = {url="...", sha256="..."}` verifies
  - [x] Test: recipe with batch fetch downloads concurrently
  - [x] Test: SHA256 mismatch fails with clear error
  - [x] Test: filename collision detected and errors early
  - [x] Test: per-file caching across partial failures
  - [x] Test: corrupted cache detection and re-download
  - [x] Test: unmarked completion (SHA256-based revalidation)
  - [x] Verify `test_fetch_function_basic` and `test_fetch_function_with_dependency` pass

**Completion Criteria:** Recipes can declaratively fetch files with optional SHA256 verification, concurrent downloads work, per-file caching handles partial failures and corruption.

**Results:** Declarative fetch fully implemented with robust caching. All 5 declarative fetch tests + 4 caching tests pass.

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

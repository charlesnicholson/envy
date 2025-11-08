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

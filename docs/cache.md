# Cache Design

## Overview
- Single cache root (default `~/.cache/envy/`) holds recipes and assets; entries become immutable once marked complete so readers never lock.
- Scene-aware locking: exclusive while building, lock-free after `envy-complete` appears; install directories live beside final paths for atomic rename.
- Project-local (`local.*`) recipes stay in the repo tree and bypass the shared cache.

## Layout
```
~/.cache/envy/
├── recipes/                      # Lua sources (declarative and custom fetch)
│   ├── envy.cmake@v1.lua         # Single-file declarative recipe
│   ├── arm.gcc@v2.lua
│   └── corporate.toolchain@v3/   # Multi-file (custom fetch or archive)
│       ├── envy-complete
│       ├── recipe.lua            # Entry point (required)
│       ├── helpers.lua
│       ├── fetch/                # Durable fetch cache (persists across failures)
│       │   └── envy-complete     # Marker: all fetches verified
│       └── work/                 # Ephemeral workspace (wiped each attempt)
├── assets/                       # Asset entries (one per identity/options/platform)
│   └── {namespace}.{name}@{version}/
│       └── {platform}-{arch}-sha256-{hash}/
│           ├── envy-complete
│           ├── envy-fingerprint.blake3
│           ├── asset/            # Publish-ready payload (renamed from install/)
│           ├── fetch/            # Durable fetch cache (persists for per-file caching)
│           │   └── envy-complete # Marker: all fetches verified
│           ├── install/          # Staging area for asset preparation
│           └── work/             # Ephemeral workspace (stage/, etc.)
│               └── stage/        # Build staging tree (wiped before each attempt)
└── locks/
    └── {recipe|asset}.*.lock
```

## Keys
- **Recipe**: `{namespace}.{name}@{version}.lua` for single-file declarative sources, `{namespace}.{name}@{version}/` for multi-file (custom fetch, archives, git repos). Custom fetch recipes always use directory layout with `recipe.lua` entry point.
- **Asset**: `{identity}.{platform}-{arch}-sha256-{hash}` where `hash` is the leading 16 hex chars of the archive SHA256; deterministic before download so locks can be acquired early.

## Locking & Workspace Lifecycle

### General Lock Behavior
- Locks: POSIX `fcntl(F_SETLKW)` / Windows `LockFileEx`; blocking exclusive locks only during creation.
- Patterns:
  1. Lock-free read: check `envy-complete`; if present, return path immediately.
  2. Exclusive creation: grab lock, re-check marker (someone else may have finished), proceed if still missing.
- Lock files (`locks/...`) exist only while holding the lock—`cache::scoped_entry_lock` destroys them on destruction.

### Cache-Managed Packages (Standard)
- Acquisition ensures `assets/{entry}/install/` and `assets/{entry}/work/` exist.
- Workspace separates `fetch/` (durable, persists across failures) and `work/` (ephemeral, wiped each attempt).
- Per-file caching: `fetch/` persists across failed attempts. On subsequent runs, each file is verified by SHA256 before re-downloading. Only missing or corrupted files trigger new downloads. Files without SHA256 are always re-downloaded (no cache trust without verification).
- Recipes call `mark_fetch_complete()` once all fetches succeed; this drops `envy-complete` sentinel inside `fetch/`.
- On success (recipe calls `mark_install_complete()`):
  - Envy atomically renames `install/` → `asset/`
  - Fingerprints payload into `envy-fingerprint.blake3`
  - Deletes both `work/` and `fetch/`
  - Touches `entry/envy-complete`
- Crash recovery: next locker deletes stale `install/`, recreates `work/`, preserves `fetch/` for per-file cache reuse.

### User-Managed Packages (Ephemeral Cache)
- Identified by presence of check verb in recipe
- Lock calls `mark_user_managed()` to signal ephemeral workspace
- Workspace created same as cache-managed: `install/`, `fetch/`, `work/` directories
- Recipe can use all phases (fetch/stage/build/install) for workspace operations
- Install phase must NOT call `mark_install_complete()` (validated at runtime)
- On completion (lock destructor detects `user_managed_` flag):
  - Entire `entry_dir` deleted (no `asset/` rename)
  - Cache entry fully purged—no persistent artifacts
  - Lock file deleted
- Subsequent runs use check verb (not cache marker) to skip work
- Crash recovery: next locker deletes entire stale entry, starts fresh

### Lock Destructor Three-Way Branch
The `scoped_entry_lock` destructor handles three distinct completion modes:
1. **Success (completed_):** Cache-managed package finished installation
   - Rename `install/` → `asset/`
   - Create `envy-complete` marker and fingerprint
   - Delete `work/` and `fetch/`
2. **User-managed (user_managed_):** Ephemeral workspace no longer needed
   - Delete entire `entry_dir` (all subdirectories)
   - No `envy-complete` marker created
3. **Failure (neither flag set):** Cache-managed package failed
   - Conditional purge: if `install/` and `fetch/` both empty, delete entry
   - Otherwise preserve `fetch/` for per-file cache reuse

## Integrity & Verification

**Recipes:**
- **Declarative sources (URL):** Manifest supplies SHA256; Envy downloads to temp, verifies hash, moves to cache on match. Mismatch = hard failure.
- **Declarative sources (git):** Manifest supplies `ref` (commit SHA or committish); Envy clones repo, checks out ref, verifies git tree integrity. No separate SHA256 needed (git hash is fingerprint).
- **Custom fetch functions:** API-enforced per-file verification. `ctx:fetch(url, sha256)` and `ctx:import_file(src, dest, sha256)` verify before writing to cache. Custom fetch cannot bypass (no direct cache directory access).
- **Verification timing:** SHA256 computed at fetch time only; never re-verified from cache (`envy-complete` marker signals immutable entry).

**Assets:**
- Recipes declare expected hashes for downloads; verification happens before extraction and during per-file cache reuse.
- Per-file caching: declarative fetch arrays with SHA256 verification enable cache reuse across partial failures. On each attempt, existing files in `fetch/` are verified by SHA256 before re-downloading. Cache hits skip download; cache misses (corruption, missing files) trigger re-download.
- Trust chain: once recipe passes integrity, its declared downloads inherit trust. Files without SHA256 cannot be cached (always re-downloaded).
- BLAKE3 fingerprint file captures every asset payload (mmap-friendly header, entry table, string blob) so verification tools compare without locks.

## Operational Scenarios

### Recipe Fetch

1. **Declarative single-file (first fetch):** miss → lock → download URL to temp → verify SHA256 → move to `recipes/{identity}.lua` → touch `envy-complete` (in parent dir logic) → release.

2. **Declarative git (first fetch):** miss → lock → clone repo to temp → checkout `ref` → extract tree to `recipes/{identity}/` → record git ref in `envy-git-ref` → touch `recipes/{identity}/envy-complete` → release.

3. **Custom fetch (first fetch):** miss → lock → create `recipes/{identity}/fetch/` → create temp workspace → call fetch function (ctx.tmp_dir, ctx.fetch, ctx.commit_fetch) → verify each import via SHA256 → copy verified files to `recipes/{identity}/` → touch `recipes/{identity}/fetch/envy-complete` → touch `recipes/{identity}/envy-complete` → release.

4. **Concurrent recipe fetch:** waiter blocks on lock; when creator finishes, waiter rechecks `envy-complete` and returns path without refetching.

5. **Recipe collision:** mismatched SHA256 triggers hard failure with instruction to bump recipe version.

### Asset Install

1. **First asset install**: miss → lock → create `install/` + `fetch/` + `work/` → download into `fetch/` → verify SHA256 per file → `mark_fetch_complete()` on success → stage sources in `work/stage/` → write payload into `install/` → rename to `asset/` → fingerprint `asset/` → delete `fetch/` + `work/` → touch entry `envy-complete` → release.

2. **Concurrent asset install**: waiter blocks on lock; when creator finishes, waiter rechecks `envy-complete` and returns final path without recaching.

3. **Crash recovery**: crash leaves `install/` (and maybe `fetch/` + `work/`); next locker deletes stale `install/` and `work/`, preserves `fetch/` for per-file cache reuse, and restarts. Declarative fetch verifies each cached file by SHA256 before re-downloading.

4. **Partial failure recovery**: partial download leaves some files in `fetch/`; next attempt verifies cached files by SHA256, reuses cache hits, only downloads missing/corrupted files.

5. **Multi-project sharing**: identical `(identity, options, platform, hash)` reuses the same asset directory; no duplication.

### User-Managed Package Install

1. **First install with check=false**: check verb returns false (not satisfied) → lock acquired, `mark_user_managed()` called → double-check returns false → fetch/stage/build/install phases run → install modifies system (brew install, file writes, etc.) → lock destructor detects `user_managed_` flag → entire `entry_dir` deleted (fetch/, work/, install/ all purged) → lock released.

2. **Second run with check=true**: check verb returns true (already satisfied) → no lock acquired → all phases skipped.

3. **Concurrent install**: Process A checks (false) → acquires lock, marks user-managed, re-checks (false) → starts install. Process B checks (false) → blocks on lock. Process A completes install, destructor purges entry. Process B acquires lock, marks user-managed, re-checks (NOW true, A finished) → releases lock immediately without running phases.

4. **User-managed with fetch**: check=false → lock acquired → fetch downloads files to `fetch/` → stage extracts to `work/stage/` → install runs system command → destructor purges ALL directories (fetch/, work/, install/) → no artifacts remain.

5. **Validation error**: User-managed recipe incorrectly calls `mark_install_complete()` → install phase throws runtime error: "Recipe {identity} has check verb (user-managed) but called mark_install_complete(). User-managed recipes must not populate cache. Remove check verb or remove mark_install_complete() call."

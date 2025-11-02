# Cache Design

## Overview
- Single cache root (default `~/.cache/envy/`) holds recipes and assets; entries become immutable once marked complete so readers never lock.
- Scene-aware locking: exclusive while building, lock-free after `.envy-complete` appears; install directories live beside final paths for atomic rename.
- Project-local (`local.*`) recipes stay in the repo tree and bypass the shared cache.

## Layout
```
~/.cache/envy/
├── recipes/                      # Lua sources (declarative and custom fetch)
│   ├── envy.cmake@v1.lua         # Single-file declarative recipe
│   ├── arm.gcc@v2.lua
│   └── corporate.toolchain@v3/   # Multi-file (custom fetch or archive)
│       ├── .envy-complete
│       ├── recipe.lua            # Entry point (required)
│       ├── helpers.lua
│       └── .work/
│           └── fetch/
│               └── .envy-complete
├── assets/                       # Asset entries (one per identity/options/platform)
│   └── {namespace}.{name}@{version}/
│       └── {platform}-{arch}-sha256-{hash}/
│           ├── .envy-complete
│           ├── .envy-fingerprint.blake3
│           ├── asset/            # Publish-ready payload (renamed from .install/)
│           └── .work/
│               ├── fetch/        # Durable payloads (reuse when marker valid)
│               └── stage/        # Build staging tree (wiped before each attempt)
└── locks/
    └── {recipe|asset}.*.lock
```

## Keys
- **Recipe**: `{namespace}.{name}@{version}.lua` for single-file declarative sources, `{namespace}.{name}@{version}/` for multi-file (custom fetch, archives, git repos). Custom fetch recipes always use directory layout with `recipe.lua` entry point.
- **Asset**: `{identity}.{platform}-{arch}-sha256-{hash}` where `hash` is the leading 16 hex chars of the archive SHA256; deterministic before download so locks can be acquired early.

## Locking & Workspace Lifecycle
- Locks: POSIX `fcntl(F_SETLKW)` / Windows `LockFileEx`; blocking exclusive locks only during creation.
- Patterns:
  1. Lock-free read: check `.envy-complete`; if present, return path immediately.
  2. Exclusive creation: grab lock, re-check marker (someone else may have finished), proceed if still missing.
- Lock files (`locks/...`) exist only while holding the lock—`cache::scoped_entry_lock` destroys them after `mark_complete()`.
- Acquisition ensures `assets/{entry}.install/` and `assets/{entry}.work/` exist. The workspace owns `fetch/` (durable) and `stage/` (ephemeral) subdirectories. Recipes call `mark_fetch_complete()` once a fetch succeeds; this drops a `.envy-complete` sentinel inside `work/fetch/` so later attempts can reuse the payload.
- On success Envy atomically renames `.install/` to `asset/`, fingerprints the payload into `.envy-fingerprint.blake3`, deletes `.work/`, and touches `entry/.envy-complete`.
- Crash recovery: next locker deletes stale `.install/`, recreates `work/stage/`, and either reuses or discards `work/fetch/` based on the fetch sentinel (`mark_fetch_complete()` present = reuse, otherwise refetch); no cache-wide sweeps required.

## Integrity & Verification

**Recipes:**
- **Declarative sources (URL):** Manifest supplies SHA256; Envy downloads to temp, verifies hash, moves to cache on match. Mismatch = hard failure.
- **Declarative sources (git):** Manifest supplies `ref` (commit SHA or committish); Envy clones repo, checks out ref, verifies git tree integrity. No separate SHA256 needed (git hash is fingerprint).
- **Custom fetch functions:** API-enforced per-file verification. `ctx:fetch(url, sha256)` and `ctx:import_file(src, dest, sha256)` verify before writing to cache. Custom fetch cannot bypass (no direct cache directory access).
- **Verification timing:** SHA256 computed at fetch time only; never re-verified from cache (`.envy-complete` marker signals immutable entry).

**Assets:**
- Recipes declare expected hashes for downloads; verification happens before extraction.
- Trust chain: once recipe passes integrity, its declared downloads inherit trust.
- BLAKE3 fingerprint file captures every asset payload (mmap-friendly header, entry table, string blob) so verification tools compare without locks.

## Operational Scenarios

### Recipe Fetch

1. **Declarative single-file (first fetch):** miss → lock → download URL to temp → verify SHA256 → move to `recipes/{identity}.lua` → touch `.envy-complete` (in parent dir logic) → release.

2. **Declarative git (first fetch):** miss → lock → clone repo to temp → checkout `ref` → extract tree to `recipes/{identity}/` → record git ref in `.envy-git-ref` → touch `recipes/{identity}/.envy-complete` → release.

3. **Custom fetch (first fetch):** miss → lock → create `recipes/{identity}/.work/fetch/` → create temp workspace → call fetch function (ctx:work_dir, ctx:fetch, ctx:import_file) → verify each import via SHA256 → copy verified files to `recipes/{identity}/` → touch `recipes/{identity}/.work/fetch/.envy-complete` → touch `recipes/{identity}/.envy-complete` → release.

4. **Concurrent recipe fetch:** waiter blocks on lock; when creator finishes, waiter rechecks `.envy-complete` and returns path without refetching.

5. **Recipe collision:** mismatched SHA256 triggers hard failure with instruction to bump recipe version.

### Asset Install

1. **First asset install**: miss → lock → create `.install/` + `.work/` → download into `work/fetch/` → `mark_fetch_complete()` on success → stage sources in `work/stage/` → write payload into `.install/` → rename to `asset/` → fingerprint `asset/` → delete `.work/` → touch entry `.envy-complete` → release.

2. **Concurrent asset install**: waiter blocks on lock; when creator finishes, waiter rechecks `.envy-complete` and returns final path without recaching.

3. **Crash recovery**: crash leaves `.install/` (and maybe `.work/`); next locker deletes stale `.install/`, conditionally reuses `work/fetch/`, and restarts staging.

4. **Multi-project sharing**: identical `(identity, options, platform, hash)` reuses the same asset directory; no duplication.

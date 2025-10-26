# Cache Design

## Overview
- Single cache root (default `~/.cache/envy/`) holds recipes and assets; entries become immutable once marked complete so readers never lock.
- Scene-aware locking: exclusive while building, lock-free after `.envy-complete` appears; staging directories live beside final paths for atomic rename.
- Project-local (`local.*`) recipes stay in the repo tree and bypass the shared cache.

## Layout
```
~/.cache/envy/
├── recipes/                      # Lua sources and archives
│   ├── envy.cmake@v1.lua
│   ├── arm.gcc@v2.lua
│   └── gnu.binutils@v3/
│       ├── recipe.lua
│       └── helpers.lua
├── assets/                       # Asset entries (one per identity/options/platform)
│   └── {namespace}.{name}@{version}/
│       └── {platform}-{arch}-sha256-{hash}/
│           ├── .envy-complete
│           ├── .envy-fingerprint.blake3
│           ├── .staging/         # Publish-ready tree (renamed into place on success)
│           └── .work/            # Download/build workspace (cleared after success)
└── locks/
    └── {recipe|asset}.*.lock
```

## Keys
- **Recipe**: `{namespace}.{name}@{version}.lua` for single-file, `{namespace}.{name}@{version}/` for archives.
- **Asset**: `{identity}.{platform}-{arch}-sha256-{hash}` where `hash` is the leading 16 hex chars of the archive SHA256; deterministic before download so locks can be acquired early.

## Locking & Staging
- Locks: POSIX `fcntl(F_SETLKW)` / Windows `LockFileEx`; blocking exclusive locks only during creation.
- Patterns:
  1. Lock-free read: check `.envy-complete`; if present, return path immediately.
  2. Exclusive creation: grab lock, re-check marker (someone else may have finished), proceed if still missing.
- Lock files (`locks/...`) exist only while holding the lock—`cache::scoped_entry_lock` destroys them after `mark_complete()`.
- Acquisition ensures both `assets/{entry}.staging/` and `assets/{entry}.work/` exist. The `.work/` directory hosts downloads/build artifacts; `.staging/` holds only the final install prefix.
- Commit writes marker inside `.staging/`, removes transient build products under `.work/`, renames `.staging/` to the final asset directory, and only then releases the lock.
- Crash recovery: next locker blows away stale `.staging/` and, unless a fetch marker says otherwise, resets `.work/` before retrying downloads; no global sweeps.

## Integrity & Verification
- Recipes: manifest must supply SHA256; envy verifies before caching/executing. Optional opt-out via `allow_unverified_recipes`.
- Assets: recipes declare expected hashes; verification happens before extraction.
- Trust chain: once a recipe passes integrity, its declared downloads inherit trust.
- BLAKE3 fingerprint file captures every asset payload (mmap-friendly header, entry table, string blob) so verification tools compare without locks.

## Operational Scenarios
1. **First asset install**: miss → lock → bootstrap `.work/` + `.staging/` → download/build inside `.work/` → install into `.staging/` → fingerprint → `mark_complete()` → atomic rename → wipe `.work/` → release → future reads go lock-free.
2. **Concurrent asset install**: waiter blocks on lock; when creator finishes, waiter rechecks `.envy-complete` and returns final path without recaching.
3. **Crash recovery**: crash leaves `.staging/` (and maybe `.work/`); next locker deletes stale `.staging/`, optionally reuses `.work/` if fetch markers are valid, otherwise re-downloads.
4. **Multi-project sharing**: identical `(identity, options, platform, hash)` reuses the same asset directory; no duplication.
5. **Recipe collision**: mismatched SHA256 triggers hard failure with instruction to bump recipe version.

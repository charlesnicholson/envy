# Envy Architecture

## Filesystem Cache

### Overview

Envy maintains a user-wide cache at `~/.cache/envy/` containing recipes (Lua scripts describing packages) and deployed assets (unpacked toolchains, SDKs, etc.). The cache is immutable: once an entry is marked complete, its content never changes. This immutability enables lock-free reads while using shared/exclusive locks only during creation to prevent duplicate work across concurrent envy processes.

### Directory Structure

```
~/.cache/envy/
├── recipes/
│   ├── community/          # Shared recipes from community sources
│   │   └── {name}/
│   │       └── {version}/
│   │           ├── recipe.lua
│   │           └── .envy-meta
│   ├── builtin/            # Recipes embedded in envy executable
│   │   ├── .version
│   │   └── {name}/
│   │       └── {version}/
│   │           └── recipe.lua
│   └── local/              # Project-specific recipes
│       └── {name}/
├── deployed/
│   └── {name}/
│       └── {version}/
│           └── {platform}-{arch}-sha256-{hash}/
│               ├── .envy-complete
│               ├── .envy-fingerprint.blake3
│               └── [unpacked asset tree]
└── locks/
    ├── recipe.{namespace}.{name}.{version}.lock
    ├── deployed.{name}.{version}.{platform}-{arch}-sha256-{hash}.lock
    └── builtin.extraction.lock
```

### Cache Keys

**Recipe identity:** `{namespace}.{name}.{version}` where namespace is one of `community`, `builtin`, or `local`.

**Deployment identity:** `{name}.{version}.{platform}-{arch}-sha256-{hash}` where:
- `platform` is `darwin`, `linux`, or `windows`
- `arch` is `x86_64`, `arm64`, etc.
- `hash` is the first 16 hex characters of the SHA256 of the fetched archive

The deployment key is fully determined by recipe metadata before any download begins, enabling lock acquisition prior to expensive operations.

### Immutability Contract

Once `.envy-complete` marker file exists in a cache entry, that entry is immutable forever. Entries are only removed during explicit cache flush operations (future). This immutability eliminates the need for locks during read operations—checking for `.envy-complete` and using the entry requires no coordination between processes.

### Lock-Based Concurrency

#### Lock Semantics

**Exclusive lock:** Single process holds lock while creating a cache entry (downloading, extracting, computing fingerprints). Blocks all other lock acquisition attempts.

**Shared lock:** Multiple processes can hold simultaneously. Used for waiting (blocks until exclusive holder releases) and for reading (though immutable reads need no lock after `.envy-complete` exists).

#### Platform-Specific Implementation

**Linux/macOS:** POSIX advisory locks via `fcntl` with `F_SETLK` (non-blocking) and `F_SETLKW` (blocking). Use `F_RDLCK` for shared locks, `F_WRLCK` for exclusive. Locks are automatically released when file descriptor is closed or process terminates.

**Windows:** Byte-range locking via `LockFileEx`/`UnlockFileEx` with `OVERLAPPED` structure. Use `LOCKFILE_EXCLUSIVE_LOCK` flag for exclusive locks, omit for shared. Use `LOCKFILE_FAIL_IMMEDIATELY` for non-blocking attempts. Locks are automatically released when handle is closed or process terminates.

#### Lock Abstraction

Provide cross-platform `CacheLock` class with methods:
- `try_acquire(LockMode mode)` → non-blocking attempt
- `acquire(LockMode mode, timeout)` → blocking with timeout
- `release()` → explicit release (also automatic in destructor)

Lock files are regular empty files in `locks/` directory. File content is unused; only the lock state matters.

### Concurrency Patterns

#### Pattern: Lock-Free Read

Check if target directory contains `.envy-complete` marker. If yes, use immediately without acquiring any locks. All waiters and future processes take this fast path once the first process completes deployment.

#### Pattern: Shared Lock Wait

When target doesn't exist, attempt to acquire shared lock. If acquisition succeeds immediately, no other process is working—upgrade to exclusive lock and perform the work. If acquisition blocks, another process holds exclusive lock—wait for lock release (signaling completion or failure), then recheck for `.envy-complete`.

#### Pattern: Exclusive Lock Work

After acquiring exclusive lock, create `.inprogress/` staging directory in parent of target. Perform all expensive operations (download, extract, compute BLAKE3 fingerprints) in staging area. Write `.envy-complete` marker as final step. Atomically rename staging directory to target name. Release exclusive lock, which unblocks all shared lock waiters simultaneously.

### BLAKE3 Fingerprint File

After extracting an asset, compute BLAKE3 hash of every regular file in the deployment tree. Store results in `.envy-fingerprint.blake3` using a binary format optimized for mmap:

**File format:**
- Fixed-size header containing magic bytes, format version, file count, and offset to file index
- Variable-length body containing array of entries (path offset/length, 32-byte BLAKE3 hash, file size, mtime)
- String table of null-terminated UTF-8 paths

User-initiated verification maps this file read-only and compares stored hashes against current file contents without acquiring locks (immutable reads).

### Recipe Metadata

Each recipe directory contains `.envy-meta` file with:
- `hash`: BLAKE3 hash of `recipe.lua` content
- `source_url`: Where recipe was fetched from
- `fetched_at`: Unix timestamp of fetch

Recipe hash is computed after fetching but before making available. Ensures recipe identity includes its content, not just name and version.

### Built-in Recipe Extraction

Envy executable embeds default recipes as compressed data. On first run or version upgrade, extract to `recipes/builtin/`. Use `builtin.extraction.lock` to coordinate extraction across concurrent first-run processes. Check `.version` file matches embedded version string before acquiring lock (fast path). After extraction, write `.version` file to prevent redundant work on subsequent runs.

### Use Cases

#### Use Case: First Deployment of Asset

- Process checks `deployed/{name}/{version}/{platform}-{arch}-sha256-{hash}/.envy-complete`
- Marker doesn't exist
- Acquires exclusive lock on `deployed.{name}.{version}.{platform}-{arch}-sha256-{hash}.lock`
- Creates `.inprogress/` staging directory
- Downloads archive from URL specified in recipe
- Extracts archive into staging directory
- Walks extracted tree and computes BLAKE3 of each file
- Writes `.envy-fingerprint.blake3` with all hashes
- Writes empty `.envy-complete` marker
- Atomically renames `.inprogress/` to final deployment name
- Releases lock
- All future accesses use lock-free read path

#### Use Case: Concurrent Deployment of Same Asset

- Process A checks for `.envy-complete`, doesn't find it
- Process A acquires exclusive lock, begins download
- Process B checks for `.envy-complete`, doesn't find it
- Process B attempts shared lock acquisition, blocks
- User sees "Waiting for {name} {version} deployment..."
- Process A completes download, extraction, fingerprinting
- Process A writes `.envy-complete` and releases lock
- Process B's shared lock acquisition succeeds immediately
- Process B checks for `.envy-complete`, finds it
- Process B proceeds with lock-free read
- No duplicate download occurred

#### Use Case: Deployer Crashes Mid-Extraction

- Process A acquires exclusive lock, begins extraction into `.inprogress/`
- Process A crashes or is killed
- OS automatically releases exclusive lock
- Process B attempts shared lock, succeeds immediately (no holder)
- Process B checks for `.envy-complete`, doesn't find it
- Process B releases shared lock
- Process B acquires exclusive lock
- Process B removes stale `.inprogress/` directory
- Process B completes deployment normally

#### Use Case: Reading Deployed Asset

- Process checks `deployed/{name}/{version}/{platform}-{arch}-sha256-{hash}/.envy-complete`
- Marker exists
- Process reads files from deployment directory
- No locks acquired
- Any number of processes can read concurrently

#### Use Case: Verifying Deployed Asset Integrity

- User runs `envy verify {name}`
- Process checks for `.envy-complete` marker
- Opens `.envy-fingerprint.blake3` read-only
- Maps file into memory with `mmap`
- Iterates entries, computing BLAKE3 of current file and comparing to stored hash
- Reports any mismatches
- No locks acquired (immutable read)
- Multiple processes can verify simultaneously

#### Use Case: Fetching Community Recipe

- Process checks `recipes/community/{name}/{version}/recipe.lua`
- File doesn't exist
- Attempts shared lock on `recipe.community.{name}.{version}.lock`
- Shared lock succeeds immediately (no one fetching)
- Releases shared lock, acquires exclusive lock
- Creates `.inprogress/` staging directory
- Downloads `recipe.lua` from community repository
- Computes BLAKE3 hash of recipe content
- Writes `.envy-meta` with hash, source URL, timestamp
- Atomically renames `.inprogress/` to final version directory
- Releases lock
- Recipe is now available for all processes

#### Use Case: First Run - Extracting Built-in Recipes

- Process checks `recipes/builtin/.version`
- File doesn't exist or content doesn't match `ENVY_EMBEDDED_RECIPES_VERSION`
- Attempts shared lock on `builtin.extraction.lock`
- Shared lock succeeds, no `.version` match
- Upgrades to exclusive lock
- Extracts embedded recipes from executable data section
- Writes each recipe to `recipes/builtin/{name}/{version}/recipe.lua`
- Writes `.version` file with current embedded version string
- Releases lock
- Subsequent runs hit fast path (version matches)

#### Use Case: Multiple Projects Using Same Toolchain

- Project A in `/home/user/project-a/` runs `envy deploy`
- Deploys `arm-gcc@13.2.0` to cache
- Project B in `/home/user/project-b/` runs `envy deploy`
- Requests same `arm-gcc@13.2.0`
- Checks cache, finds `.envy-complete` marker
- Uses deployed toolchain immediately (lock-free)
- Both projects reference same cache entry
- Large toolchain stored once, shared across all projects under user account

# Envy Architecture

## Filesystem Cache

### Overview

Envy maintains a user-wide cache at `~/.cache/envy/` containing recipes (Lua scripts describing packages) and deployed assets (unpacked toolchains, SDKs, etc.). The cache is immutable: once an entry is marked complete, its content never changes. This immutability enables lock-free reads while using shared/exclusive locks only during creation to prevent duplicate work across concurrent envy processes.

### Directory Structure

```
~/.cache/envy/
├── recipes/
│   ├── envy/               # Built-in recipes embedded in envy executable
│   │   ├── .version
│   │   ├── {name}.lua
│   │   └── .envy-meta.{name}
│   └── {namespace}/        # Third-party namespaced recipes (arm, gnu, cmake, etc.)
│       ├── {name}.lua
│       └── .envy-meta.{name}
├── deployed/
│   └── {namespace}.{name}@{version}/
│       └── {platform}-{arch}-sha256-{hash}/
│           ├── .envy-complete
│           ├── .envy-fingerprint.blake3
│           └── [unpacked asset tree]
└── locks/
    ├── recipe.{namespace}.{name}.lock
    ├── deployed.{namespace}.{name}@{version}.{platform}-{arch}-sha256-{hash}.lock
    └── builtin.extraction.lock
```

**Project-local recipes** (reserved `local.*` namespace) live in the project directory tree and are never cached:
```
project/
└── .envy/
    └── recipes/
        └── custom-tool.lua
```

### Recipe Organization

**Namespaces:** All recipes must be namespaced (e.g., `envy.cmake`, `arm.gcc`, `gnu.binutils`). The `local.*` namespace is reserved for project-local recipes and is banned from the cache to prevent collisions.

**Recipe naming:** Recipe files are named `{name}.lua` where `{name}` is unique within the namespace. Authors choose whether to create version-specific recipes (`gcc13.lua`, `gcc14.lua`) or version-agnostic recipes (`gcc.lua`) that handle multiple versions via runtime validation.

**Recipe identity:** `{namespace}.{name}` uniquely identifies a recipe. Version is passed as a parameter when the recipe is invoked.

**Recipe sources:**
- **Project-local:** Referenced via `require("./path/to/recipe.lua")` in project manifest, never cached
- **Remote:** Declared in manifest with URL and optional SHA256 hash, fetched and cached on demand
- **Built-in:** Embedded in envy executable using `envy.*` namespace, extracted to cache on first run

### Cache Keys

**Recipe identity:** `{namespace}.{name}` where namespace is any valid identifier except `local` (reserved for project-local recipes).

**Deployment identity:** `{namespace}.{name}@{version}.{platform}-{arch}-sha256-{hash}` where:
- `namespace.name` is the recipe identity
- `version` is the specific version being deployed (e.g., `13.2.0`)
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

Each cached recipe has an associated `.envy-meta.{name}` file containing:
- `hash`: SHA256 hash of the recipe file itself (e.g., `gcc.lua`)
- `source_url`: Where recipe was fetched from (for remote recipes)
- `fetched_at`: Unix timestamp of fetch
- `verified`: Boolean indicating whether the recipe hash was verified against a user-provided hash

Recipe hash is computed after fetching. This establishes the trust anchor for the recipe's integrity.

### Recipe Security Model

**Recipe verification:** When fetching remote recipes, the project manifest can specify an expected SHA256 hash. Envy verifies the downloaded recipe file against this hash before caching or executing. If no hash is provided, envy warns the user unless a flag is set in the manifest to allow unverified recipes.

**Asset verification:** Recipes declare SHA256 hashes for the assets (tarballs, archives) they instruct envy to download. Envy verifies these hashes before extraction.

**Trust chain:** Verifying the recipe file establishes the trust anchor. Asset hashes and deployment logic declared by a verified recipe are trusted transitively.

**Dependency validation:** Non-local recipes are forbidden from depending on `local.*` recipes. Envy enforces this as an error.

### Built-in Recipe Extraction

Envy executable embeds default recipes (using `envy.*` namespace) as compressed data. On first run or version upgrade, extract to `recipes/envy/`. Use `builtin.extraction.lock` to coordinate extraction across concurrent first-run processes. Check `.version` file matches embedded version string before acquiring lock (fast path). After extraction, write `.version` file to prevent redundant work on subsequent runs.

### Use Cases

#### Use Case: First Deployment of Asset

- Process checks `deployed/{namespace}.{name}@{version}/{platform}-{arch}-sha256-{hash}/.envy-complete`
- Marker doesn't exist
- Acquires exclusive lock on `deployed.{namespace}.{name}@{version}.{platform}-{arch}-sha256-{hash}.lock`
- Creates `.inprogress/` staging directory
- Downloads archive from URL specified in recipe
- Verifies SHA256 hash against value declared in recipe (fails if mismatch)
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
- User sees "Waiting for arm.gcc 13.2.0 deployment..."
- Process A completes download, hash verification, extraction, fingerprinting
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

- Process checks `deployed/{namespace}.{name}@{version}/{platform}-{arch}-sha256-{hash}/.envy-complete`
- Marker exists
- Process reads files from deployment directory
- No locks acquired
- Any number of processes can read concurrently

#### Use Case: Verifying Deployed Asset Integrity

- User runs `envy verify arm.gcc`
- Process checks for `.envy-complete` marker
- Opens `.envy-fingerprint.blake3` read-only
- Maps file into memory with `mmap`
- Iterates entries, computing BLAKE3 of current file and comparing to stored hash
- Reports any mismatches
- No locks acquired (immutable read)
- Multiple processes can verify simultaneously

#### Use Case: Fetching Remote Recipe

- Manifest declares: `remote { namespace = "arm", name = "gcc", url = "...", sha256 = "e3b0c442..." }`
- Process checks `recipes/arm/gcc.lua`
- File doesn't exist
- Attempts shared lock on `recipe.arm.gcc.lock`
- Shared lock succeeds immediately (no one fetching)
- Releases shared lock, acquires exclusive lock
- Downloads recipe file from URL
- Computes SHA256 hash of downloaded file
- Verifies hash matches manifest-declared hash (fails if mismatch or no hash with warning)
- Writes recipe to `recipes/arm/gcc.lua`
- Computes metadata hash and writes `.envy-meta.gcc` with hash, source URL, timestamp, verified flag
- Releases lock
- Recipe is now available for all processes

#### Use Case: First Run - Extracting Built-in Recipes

- Process checks `recipes/envy/.version`
- File doesn't exist or content doesn't match `ENVY_EMBEDDED_RECIPES_VERSION`
- Attempts shared lock on `builtin.extraction.lock`
- Shared lock succeeds, no `.version` match
- Upgrades to exclusive lock
- Extracts embedded recipes from executable data section
- Writes each recipe to `recipes/envy/{name}.lua` with corresponding `.envy-meta.{name}`
- Writes `.version` file with current embedded version string
- Releases lock
- Subsequent runs hit fast path (version matches)

#### Use Case: Multiple Projects Using Same Toolchain

- Project A in `/home/user/project-a/` runs `envy deploy`
- Deploys `arm.gcc@13.2.0` to cache
- Project B in `/home/user/project-b/` runs `envy deploy`
- Requests same `arm.gcc@13.2.0`
- Checks cache, finds `.envy-complete` marker
- Uses deployed toolchain immediately (lock-free)
- Both projects reference same cache entry
- Large toolchain stored once, shared across all projects under user account

#### Use Case: Project-Local Recipe Development

- Developer creates `project/.envy/recipes/custom-arm-variant.lua`
- Project manifest includes: `require("./.envy/recipes/custom-arm-variant.lua")`
- Envy loads recipe directly from project tree
- Recipe uses `local.*` namespace or no namespace (never cached)
- Multiple builds in same project can read recipe concurrently
- Recipe is never written to `~/.cache/envy/`
- Once tested, developer can publish as `mycompany.arm-variant.lua` to shared repository

#### Use Case: Recipe Hash Mismatch

- Manifest declares: `remote { namespace = "untrusted", name = "tool", url = "...", sha256 = "abc123..." }`
- Process downloads recipe from URL
- Computes SHA256: result is `def456...`
- Mismatch detected: `def456...` ≠ `abc123...`
- Envy errors: "Recipe integrity check failed for untrusted.tool. Expected abc123..., got def456..."
- Recipe is NOT cached, NOT executed
- User must verify URL and hash, update manifest if legitimate change

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
│   │   └── {name}.lua      # No hash suffix (tied to envy binary version)
│   └── {namespace}/        # Third-party namespaced recipes (arm, gnu, cmake, etc.)
│       ├── {name}-{hash8}.lua          # Recipe file with 8-char hash prefix
│       └── .envy-meta.{name}-{hash8}   # Metadata: URL, fetch time, verified flag
├── deployed/
│   └── {namespace}.{name}@{version}/
│       └── {platform}-{arch}-sha256-{hash}/
│           ├── .envy-complete
│           ├── .envy-fingerprint.blake3
│           └── [unpacked asset tree]
└── locks/
    ├── recipe.{namespace}.{name}.{hash8}.lock
    ├── deployed.{namespace}.{name}@{version}.{platform}-{arch}-sha256-{hash}.lock
    └── builtin.extraction.lock
```

**Project-local recipes** (reserved `local.*` namespace) live in the project directory tree and are never cached:
```
project/
└── envy/
    └── recipes/
        └── custom-tool.lua
```

### Recipe Organization

**Namespaces:** All recipes must be namespaced (e.g., `envy.cmake`, `arm.gcc`, `gnu.binutils`). The `local.*` namespace is reserved for project-local recipes and is banned from the cache to prevent collisions.

**Recipe naming:** Cached remote recipes are named `{name}-{hash8}.lua` where `{hash8}` is the first 8 hexadecimal characters of the recipe's SHA256 hash. This allows multiple versions of the same recipe to coexist in the cache. Recipe authors use stable names (e.g., `gcc.lua`) and version their recipes through URL paths (Git tags, branches, or any hosting structure). Built-in recipes use `{name}.lua` without hash suffix since they're tied to the envy binary version.

**Recipe identity:** A specific cached recipe is identified by `{namespace}.{name}-{hash8}`. The `@` symbol in package declarations always refers to the **asset version** (e.g., `arm.gcc@13.2.0` means "GCC version 13.2.0"), never the recipe version. Recipe versions are controlled by the URL and pinned by the SHA256 hash.

**Recipe sources:**
- **Project-local:** Declared with `file` field in project manifest, never cached
- **Remote:** Declared with `url` and `sha256` fields, fetched and cached with hash-based filename
- **Built-in:** Embedded in envy executable using `envy.*` namespace, extracted to cache on first run

### Cache Keys

**Recipe cache key:** `{namespace}/{name}-{hash8}.lua` where:
- `namespace` is any valid identifier except `local` (reserved for project-local recipes)
- `name` is the recipe name (e.g., `gcc`, `cmake`)
- `hash8` is the first 8 hexadecimal characters of the recipe file's SHA256 hash

Multiple versions of the same recipe can coexist in the cache, distinguished by their content hash. The hash ensures immutability and allows recipe authors to version recipes via URL paths without naming conflicts. Built-in recipes use `{namespace}/{name}.lua` without hash suffix.

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

Each cached remote recipe has an associated `.envy-meta.{name}-{hash8}` file containing:
- `hash`: Full SHA256 hash of the recipe file
- `source_url`: Where recipe was fetched from
- `fetched_at`: Unix timestamp of fetch
- `verified`: Boolean indicating whether the recipe hash was verified against a user-provided hash

The hash suffix in both the recipe filename and metadata filename ensures each recipe version is immutable and independently tracked. The manifest-provided SHA256 hash establishes the trust anchor for the recipe's integrity.

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

- Manifest declares: `{ recipe = "arm.gcc@13.2.0", url = "...", sha256 = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" }`
- Process computes hash8: first 8 hex chars of SHA256 → `e3b0c442`
- Checks `recipes/arm/gcc-e3b0c442.lua`
- File doesn't exist
- Attempts shared lock on `recipe.arm.gcc.e3b0c442.lock`
- Shared lock succeeds immediately (no one fetching)
- Releases shared lock, acquires exclusive lock
- Downloads recipe file from URL
- Computes SHA256 hash of downloaded file
- Verifies hash matches manifest-declared hash (fails if mismatch)
- Writes recipe to `recipes/arm/gcc-e3b0c442.lua`
- Writes `.envy-meta.gcc-e3b0c442` with full hash, source URL, timestamp, verified flag
- Releases lock
- Recipe is now available for all processes with matching hash

#### Use Case: First Run - Extracting Built-in Recipes

- Process checks `recipes/envy/.version`
- File doesn't exist or content doesn't match `ENVY_EMBEDDED_RECIPES_VERSION`
- Attempts shared lock on `builtin.extraction.lock`
- Shared lock succeeds, no `.version` match
- Upgrades to exclusive lock
- Extracts embedded recipes from executable data section
- Writes each recipe to `recipes/envy/{name}.lua` (no hash suffix for built-ins)
- Writes `.version` file with current embedded version string
- Releases lock
- Subsequent runs hit fast path (version matches)
- Built-in recipes don't use hash suffixes since they're tied to the envy binary version

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

- Developer creates `project/envy/recipes/custom-arm-variant.lua`
- Project manifest includes: `{ recipe = "local.arm-variant@1.0.0", file = "./envy/recipes/custom-arm-variant.lua" }`
- Envy loads recipe directly from project tree
- Recipe uses `local.*` namespace (never cached)
- Multiple builds in same project can read recipe concurrently
- Recipe is never written to `~/.cache/envy/`
- Once tested, developer can publish to shared repository with URL + SHA256

#### Use Case: Recipe Hash Mismatch

- Manifest declares: `{ recipe = "untrusted.tool@1.0.0", url = "...", sha256 = "abc123..." }`
- Process downloads recipe from URL
- Computes SHA256: result is `def456...`
- Mismatch detected: `def456...` ≠ `abc123...`
- Envy errors: "Recipe integrity check failed for untrusted.tool. Expected abc123..., got def456..."
- Recipe is NOT cached, NOT executed
- User must verify URL and hash, update manifest if legitimate change

### Example Project Manifests

Project manifests support multiple syntaxes for declaring packages:

**Shorthand string syntax:** `"namespace.name@version"` expands to `{ recipe = "namespace.name", version = "version" }`

**Table with `@` in recipe field:** `{ recipe = "namespace.name@version" }` expands to `{ recipe = "namespace.name", version = "version" }`

**Explicit table syntax:** `{ recipe = "namespace.name", version = "version", ... }` is used as-is

The `@` symbol in package declarations always refers to the **asset version**, never the recipe version. Recipe versions are controlled by the URL from which they're fetched, and the SHA256 hash pins the exact recipe implementation.

#### Example 1: Embedded Firmware Project

```lua
-- project/envy.lua

packages = {
    -- Built-in recipe (shorthand string syntax)
    "envy.cmake@3.28.0",

    -- Remote recipe with verification
    {
        recipe = "arm.gcc@13.2.0",
        url = "https://github.com/arm/envy-recipes/raw/v1.0/gcc.lua",
        sha256 = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        config = {
            target = "arm-none-eabi",
            enable_lto = true,
        },
    },

    -- Project-specific custom toolchain wrapper
    {
        recipe = "local.company-wrapper@1.0.0",
        file = "./envy/recipes/company-wrapper.lua",
        config = {
            base_toolchain = "arm.gcc@13.2.0",
            extra_flags = { "-mcpu=cortex-m4", "-mthumb" },
        },
    },
}
```

#### Example 2: Cross-Platform Build Tools

```lua
-- project/envy.lua

packages = {
    -- Multiple built-in packages (shorthand syntax)
    "envy.cmake@3.28.0",
    "envy.ninja@1.11.1",

    -- Remote recipe from community maintainer
    {
        recipe = "llvm.clang@17.0.6",
        url = "https://recipes.llvm.org/clang.lua",
        sha256 = "a7c9f8b12e3d4c567890abcdef1234567890abcdef1234567890abcdef123456",
    },

    -- Remote recipe from vendor
    {
        recipe = "gnu.binutils@2.41.0",
        url = "https://gnu.org/envy/binutils.lua",
        sha256 = "b8d7e9f0a1234567890abcdef1234567890abcdef1234567890abcdef123456",
    },

    -- Project-local testing recipe (under development)
    {
        recipe = "local.linker-script-gen@2.1.0",
        file = "./envy/recipes/custom-linker-script-generator.lua",
        config = {
            memory_layout = "./config/memory.yaml",
        },
    },
}
```

#### Example 3: Unverified Remote Recipe (Development/Testing)

```lua
-- project/envy.lua

-- Allow unverified recipes (not recommended for production)
allow_unverified_recipes = true

packages = {
    "envy.cmake@3.28.0",

    -- Remote recipe without hash (will warn, but allowed due to config above)
    {
        recipe = "experimental.new-tool@0.1.0-alpha",
        url = "https://example.com/experimental/new-tool.lua",
        -- No sha256 provided - risky!
    },

    -- Local development recipe
    {
        recipe = "local.wrapper@1.0.0",
        file = "./envy/recipes/wrapper.lua",
    },
}
```

#### Example 4: Multi-Version Recipe Usage

```lua
-- project/envy.lua

packages = {
    "envy.cmake@3.28.0",

    -- Use version-agnostic recipe with different GCC versions
    {
        recipe = "gnu.gcc@13.2.0",
        url = "https://gnu.org/envy/gcc.lua",  -- Handles multiple GCC versions
        sha256 = "c9d0e1f2a3b4567890abcdef1234567890abcdef1234567890abcdef123456",
        config = {
            variant = "native",
        },
    },

    -- Cross-compilation toolchain (same recipe file, different asset version)
    {
        recipe = "gnu.gcc@12.3.0",
        url = "https://gnu.org/envy/gcc.lua",  -- Same recipe file
        sha256 = "c9d0e1f2a3b4567890abcdef1234567890abcdef1234567890abcdef123456",
        config = {
            variant = "cross",
            target = "aarch64-linux-gnu",
        },
    },

    -- If recipe needs to change for new GCC versions, author publishes new recipe version:
    -- {
    --     recipe = "gnu.gcc@14.0.0",
    --     url = "https://gnu.org/envy/gcc-v2.lua",  -- New recipe for GCC 14+
    --     sha256 = "def456...",  -- Different hash = different cached recipe
    -- },
}
```

These examples demonstrate:
- **Built-in packages** (`envy.*`) are always available without specifying URL
- **Remote packages** specify `url` and `sha256` fields
- **Project-local packages** specify `file` field and never touch the cache
- **Shorthand syntax** for simple cases: `"namespace.name@version"`
- **Asset version** (after `@`) is what you want to deploy (e.g., GCC 13.2.0)
- **Recipe version** controlled by URL path (e.g., `/v1.0/gcc.lua` vs `/v2.0/gcc.lua`)
- **Security model** allows unverified recipes with `allow_unverified_recipes = true`
- **Mixed sources** can coexist in a single project
- **List structure** represents an unordered set of required packages

### Example Recipe Files

#### Example Recipe: Version-Agnostic with Fetch Function

```lua
-- envy/recipes/arm-gcc.lua (or cached as arm/gcc.lua)

namespace = "arm"
name = "gcc"

-- Fetch can be a function that processes the version parameter
fetch = function(version)
    -- Validate version
    assert(version:match("^13%."), "This recipe only supports ARM GCC 13.x")

    -- Construct download URL based on version
    local base_url = "https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads"
    local platform = get_platform()  -- Built-in function provided by envy
    local arch = get_arch()          -- Built-in function provided by envy

    return download {
        url = string.format("%s/%s/arm-gnu-toolchain-%s-%s-%s-arm-none-eabi.tar.xz",
                           base_url, version, version, platform, arch),
        sha256 = lookup_hash(version, platform, arch),  -- Helper function in recipe
    }
end

function lookup_hash(version, platform, arch)
    local hashes = {
        ["13.2.0"] = {
            ["darwin-arm64"] = "a1b2c3d4e5f6...",
            ["linux-x86_64"] = "b2c3d4e5f6a7...",
        },
        ["13.1.0"] = {
            ["darwin-arm64"] = "c3d4e5f6a7b8...",
            ["linux-x86_64"] = "d4e5f6a7b8c9...",
        },
    }

    return hashes[version][platform .. "-" .. arch]
           or error("No hash available for " .. version .. " on " .. platform .. "-" .. arch)
end

deploy = function(ctx)
    ctx.extract_all()
    ctx.add_to_path("bin")
end
```

#### Example Recipe: Simple String Fetch

```lua
-- Cached as envy/cmake.lua

namespace = "envy"
name = "cmake"

-- Fetch can be a simple string for static URLs
fetch = "https://github.com/Kitware/CMake/releases/download/v${version}/cmake-${version}-${platform}-${arch}.tar.gz"

-- Or a function that returns a string (envy will substitute variables)
fetch = function(version)
    assert(version:match("^3%.2[0-9]%."), "Only CMake 3.2x supported")
    return "https://github.com/Kitware/CMake/releases/download/v${version}/cmake-${version}-${platform}-${arch}.tar.gz"
end

deploy = function(ctx)
    ctx.extract_all()
    ctx.add_to_path("bin")
end
```

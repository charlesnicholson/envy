# Envy Architecture

## Filesystem Cache

Envy maintains a user-wide cache at `~/.cache/envy/` containing recipes (Lua scripts) and deployed assets (toolchains, SDKs). Cache entries are immutable once marked complete—enabling lock-free reads while using shared/exclusive locks only during creation.

### Directory Structure

```
~/.cache/envy/
├── recipes/
│   ├── envy.cmake@v1.lua                   # Built-in single-file
│   ├── arm.gcc@v2.lua                      # Remote single-file
│   ├── gnu.binutils@v3/                    # Multi-file (extracted archive)
│   │   ├── recipe.lua
│   │   └── helpers.lua
│   └── .envy-meta.arm.gcc@v2               # Metadata: URL, SHA256, fetch time
├── deployed/
│   └── {namespace}.{name}@{version}/
│       └── {platform}-{arch}-sha256-{hash}/
│           ├── .envy-complete
│           ├── .envy-fingerprint.blake3
│           └── [unpacked asset tree]
└── locks/
    └── {recipe|deployed}.*.lock
```

**Project-local recipes** (`local.*` namespace) live in project tree, never cached:
```
project/envy/recipes/
├── local.tool@v1.lua
└── local.complex@v2/
    ├── recipe.lua
    └── helpers.lua
```

### Recipe Organization

**Identity:** Recipes are namespaced with version: `arm.gcc@v2`, `gnu.binutils@v3`. The `@` symbol denotes **recipe version**, not asset version. Asset versions come from `options` in manifest. Multiple recipe versions coexist; `local.*` namespace reserved for project-local recipes.

**Sources:**
- **Built-in:** Embedded in binary (`envy.*` namespace), extracted to cache on first run
- **Remote:** Fetched from `url`, verified via `sha256`, cached
- **Project-local:** Loaded from `file` path, never cached

**Formats:**
- **Single-file:** `.lua` file (default, preferred)
- **Multi-file:** Archive (`.tar.gz`/`.tar.xz`/`.zip`) with `recipe.lua` entry point at root

**Integrity:** Remote recipes require SHA256 hash in manifest. Envy verifies before caching/executing. Mismatch causes hard failure.

### Cache Keys

**Recipe:** `{namespace}.{name}@{version}.lua` or `{namespace}.{name}@{version}/`

**Deployment:** `{namespace}.{name}@{recipe_version}.{platform}-{arch}-sha256-{hash}` where `hash` is first 16 hex chars of archive SHA256. Key is deterministic before download, enabling early lock acquisition.

### Immutability & Locking

Once `.envy-complete` exists, entry is immutable. Future reads are lock-free.

**Lock types:**
- **Exclusive:** Held during cache entry creation (download/extract/fingerprint). Blocks all other acquisitions.
- **Shared:** Multiple holders allowed. Used for waiting or coordination.

**Implementation:**
- Linux/macOS: POSIX `fcntl` (`F_SETLK`/`F_SETLKW`, `F_RDLCK`/`F_WRLCK`)
- Windows: `LockFileEx`/`UnlockFileEx` with `LOCKFILE_EXCLUSIVE_LOCK` flag

**Patterns:**
1. **Lock-free read:** Check `.envy-complete`, use immediately if present
2. **Shared wait:** Attempt shared lock; if immediate success, upgrade to exclusive and work; if blocks, another process is working—wait then recheck
3. **Exclusive work:** Create `.inprogress/` staging, download/extract/fingerprint, write `.envy-complete`, atomic rename to final path, release lock

### BLAKE3 Fingerprints

`.envy-fingerprint.blake3` stores BLAKE3 hash of every file in deployment. Binary format optimized for mmap: fixed header (magic/version/count/offsets), entry array (path offset/length, 32-byte hash, size, mtime), string table. User verification maps read-only, compares hashes without locks.

### Recipe Dependencies

Recipes declare dependencies; transitive resolution is automatic. Manifest authors specify only direct needs.

```lua
-- vendor.python@v2
identity = "vendor.python@v2"
depends = { "envy.homebrew@v4" }

fetch = function(options)
    local brew_path = asset("envy.homebrew@v4")  -- Access deployed dependency
    local version = options.version or "3.11.0"
    -- Fetch logic...
end
```

**Resolution:** Topological sort ensures dependencies deploy before dependents. Cycles error (must be DAG). Dependencies specify exact recipe versions—same recipe version always uses same deps (reproducible builds).

**Security:** Non-local recipes cannot depend on `local.*` recipes. Envy enforces at load time.

### Security Model

**Recipe verification:** Manifest provides SHA256; envy verifies before cache/execute. Missing hash warns unless `allow_unverified_recipes = true`.

**Asset verification:** Recipes declare SHA256 for downloads; envy verifies before extraction.

**Trust chain:** Verified recipe establishes trust anchor. Assets/logic declared by verified recipe are trusted transitively.

### Use Cases

**First deployment:** Check `.envy-complete` (missing) → acquire exclusive lock → stage in `.inprogress/` → download → verify SHA256 → extract → compute BLAKE3 fingerprints → write `.envy-complete` → atomic rename → release lock. Future reads lock-free.

**Concurrent deployment:** Process A acquires exclusive, begins work. Process B blocks on shared lock, waits. A completes, writes `.envy-complete`, releases. B unblocks, finds `.envy-complete`, proceeds lock-free. No duplicate download.

**Crash recovery:** Process A crashes mid-extraction. OS releases lock. Process B acquires exclusive, removes stale `.inprogress/`, completes normally.

**Multi-project sharing:** Projects A and B both request `arm.gcc@v2` with `options.version="13.2.0"`. Same deployment key → cache hit → zero duplication.

**Recipe collision:** Projects A and B declare `arm.gcc@v2` with different SHA256 hashes. First to run caches recipe. Second project loads, computes hash, detects mismatch, errors: "Recipe integrity failed. Author should publish new version (e.g., arm.gcc@v3)."

## Project Manifests

**Syntax:** Shorthand `"namespace.name@version"` expands to `{ recipe = "namespace.name@version" }`. Table syntax supports `url`, `sha256`, `file`, `options`.

```lua
-- project/envy.lua
packages = {
    "envy.cmake@v1",  -- Built-in (shorthand)

    {  -- Remote with verification and options
        recipe = "arm.gcc@v2",
        url = "https://github.com/arm/recipes/gcc-v2.lua",
        sha256 = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        options = { version = "13.2.0", target = "arm-none-eabi" },
    },

    {  -- Project-local (development)
        recipe = "local.wrapper@v1",
        file = "./envy/recipes/wrapper.lua",
        options = { base = "arm.gcc@v2" },
    },
}
```

## Platform-Specific Recipes

Recipes run only on host platform—no cross-deployment. Single recipe file adapts via platform variables envy provides. Authors structure platform logic however they want.

**Envy-provided globals:**
- `ENVY_PLATFORM` — `"darwin"`, `"linux"`, `"windows"`
- `ENVY_ARCH` — `"arm64"`, `"x86_64"`, `"aarch64"`, etc.
- `ENVY_PLATFORM_ARCH` — Combined: `"darwin-arm64"`, `"linux-x86_64"`
- `ENVY_OS_VERSION` — `"14.0"` (macOS), `"22.04"` (Ubuntu)

**Single-file with conditionals:**
```lua
identity = "arm.gcc@v2"

fetch = function(options)
    local version = options.version or "13.2.0"
    local hashes = {
        ["13.2.0"] = {
            ["darwin-arm64"] = "a1b2...", ["linux-x86_64"] = "c3d4...",
        },
    }

    return {
        url = string.format("https://arm.com/gcc-%s-%s-%s.tar.xz",
                           version, ENVY_PLATFORM, ENVY_ARCH),
        sha256 = hashes[version][ENVY_PLATFORM_ARCH],
    }
end

deploy = function(ctx)
    ctx.extract_all()
    ctx.add_to_path("bin")
    if ENVY_PLATFORM == "darwin" then
        ctx.fixup_macos_rpaths()
    end
end
```

**Multi-file with platform modules:**
```
arm.gcc@v2/
├── recipe.lua
├── darwin.lua
├── linux.lua
└── checksums.lua
```

```lua
-- recipe.lua
identity = "arm.gcc@v2"
local impl = require(ENVY_PLATFORM)  -- Loads darwin.lua or linux.lua
fetch = function(opts) return impl.fetch(opts, require("checksums")) end
deploy = impl.deploy
```

**Platform validation:**
```lua
local SUPPORTED = { darwin = { arm64 = true }, linux = { x86_64 = true } }
assert(SUPPORTED[ENVY_PLATFORM] and SUPPORTED[ENVY_PLATFORM][ENVY_ARCH],
       "Unsupported platform: " .. ENVY_PLATFORM_ARCH)
```

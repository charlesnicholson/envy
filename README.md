# envy

## What is Envy?

Envy is a freeform package manager driven by Lua scripts. "Freeform" means Envy remains unopinionated about what a package represents; authors describe packages entirely in terms of verbs exposed to Lua. Core verbs are `CHECK`, `FETCH`, `STAGE`, `BUILD`, and `INSTALL` (all globals are uppercase), plus helpers for working with the user-wide cache. The cache defaults to `~/.cache/envy/` so any number of projects can share large payloads without duplication.

Envy manages pre-existing tools, built shared or static libraries, and system-wide software like Python or Homebrew. It handles everything from small utilities to sizeable toolchains like arm-gcc, llvm-clang, or SEGGER J-Link. Envy is deeply parallelized and tuned for efficient transfers and staging, letting high-throughput workflows share a single cache without blocking each other.

The project prioritizes lean binaries and predictable performance: we use straightforward, well-understood concurrency primitives (via oneTBB) and avoid gratuitous template metaprogramming or other "clever" tricks that hurt readability and optimization. Every component is selected and configured to minimize size while retaining full HTTPS/SSH functionality, with TLS 1.3 and hashing implemented through mbedTLS.

## Requirements

- CMake 4.1.2+
- Ninja build system
- C++20 compiler (MSVC on Windows)

## Quick Start

Build using the provided scripts:

```bash
./build.sh          # Unix-like systems
build.bat           # Windows
```

The build script lazily configures `out/build` if needed and builds all targets including `envy` and unit tests. Run `out/build/envy` to verify the build.

## Using Envy in Your Project

Initialize a project with `envy init`:

```bash
# Download envy binary (one-time)
curl -fsSL -o /tmp/envy https://github.com/anthropics/envy/releases/latest/download/envy-darwin-arm64
chmod +x /tmp/envy

# Initialize your project
mkdir my-project && cd my-project
/tmp/envy init . ./tools

# From now on, use the bootstrap script
./tools/envy sync
```

This creates:
- `./envy.lua` — Manifest with `@envy version` directive
- `./tools/envy` — Bootstrap script (or `envy.bat` on Windows)
- `./.luarc.json` — IDE config for lua_ls autocompletion

The bootstrap script downloads and caches the pinned envy version automatically. Teammates clone and run `./tools/envy sync`—no manual installation needed.

### Upgrading Envy

Edit the `@envy version` directive in `envy.lua`:

```lua
-- @envy version "1.4.0"
```

Next `./tools/envy` invocation downloads the new version.

### Enterprise/Air-Gapped Environments

Use `--mirror` to point to an internal server:

```bash
/tmp/envy init . ./tools --mirror=https://internal.corp/envy-releases
```

Or set `ENVY_MIRROR` environment variable for the bootstrap script.

## Cache Location

Envy uses a user-wide cache to share packages across projects. Cache root resolution (highest to lowest priority):

1. `ENVY_CACHE_ROOT` environment variable
2. `@envy cache` directive in manifest
3. Platform default:
   - **macOS**: `~/Library/Caches/envy`
   - **Linux**: `$XDG_CACHE_HOME/envy` (or `~/.cache/envy`)
   - **Windows**: `%LOCALAPPDATA%\envy`

### Cache Structure

```
$CACHE_ROOT/
├── envy/          # Cached envy binaries + type definitions
│   ├── 1.2.3/
│   │   ├── envy       # Binary
│   │   └── envy.lua   # lua_ls type definitions
│   └── 1.4.0/
├── assets/        # Installed package artifacts
├── recipes/       # Cached recipe files
└── locks/         # Lock files for concurrent access
```

### Cache Cleanup

Remove old envy versions:

```bash
rm -rf ~/Library/Caches/envy/envy/1.2.3  # Remove specific version
```

Remove all cached packages (fresh start):

```bash
rm -rf ~/Library/Caches/envy
```

## Project Structure

- `src/` — Runtime sources
- `cmake/` — Build configuration and dependency modules
- `test_data/` — Test fixtures organized by subsystem
- `functional_tests/` — Python-based functional tests
- `out/build/` — Build artifacts (CMake cache, objects, binaries)
- `out/cache/` — Cached vendored dependencies

Delete `out/build` to force a rebuild; delete `out/` entirely to restore a pristine tree.

See `AGENTS.md` for contributor guidance and `docs/architecture.md` for system design.

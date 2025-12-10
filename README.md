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

## Cache Location

Envy uses a user-wide cache to share packages across projects. The default cache root varies by platform:

- **macOS**: `~/Library/Caches/envy`
- **Linux**: `$XDG_CACHE_HOME/envy` (or `~/.cache/envy` if `XDG_CACHE_HOME` is unset)
- **Windows**: `%LOCALAPPDATA%\envy` (or `%USERPROFILE%\AppData\Local\envy` if `LOCALAPPDATA` is unset)

Override the default by setting the `ENVY_CACHE_ROOT` environment variable.

## Project Structure

- `src/` — Runtime sources
- `cmake/` — Build configuration and dependency modules
- `test_data/` — Test fixtures organized by subsystem
- `functional_tests/` — Python-based functional tests
- `out/build/` — Build artifacts (CMake cache, objects, binaries)
- `out/cache/` — Cached vendored dependencies

Delete `out/build` to force a rebuild; delete `out/` entirely to restore a pristine tree.

See `AGENTS.md` for contributor guidance and `docs/architecture.md` for system design.

# envy-cmake-test

Static macOS test driver that bundles libgit2, libcurl (OpenSSL), libssh2, OpenSSL, Lua, oneTBB, libarchive, BLAKE3, and the AWS SDK for C++ (S3-only) into a single executable using modern CMake. The goal is to build each dependency from source without Git submodules, link everything statically, and exercise the libraries.

## What is Envy?

Envy is a freeform package manager driven by Lua scripts. “Freeform” means Envy remains unopinionated about what a package represents; authors describe packages entirely in terms of verbs exposed to Lua. Core verbs include `fetch`, `cache`, `deploy`, `asset` (for locating cached assets), `check`, and `update` (to ensure a project stays current). The runtime ships with a cache that defaults to being user-wide so any number of projects under a user’s workspace can share large payloads.

The primary use cases revolve around staging sizeable toolchains—think arm-gcc, llvm-clang, or the SEGGER J-Link suites—but the same primitives can install machine-global software such as Python or even orchestrate Homebrew on macOS. Envy is deeply parallelized and tuned for efficient transfers and staging, letting high-throughput workflows share a single cache without blocking each other.

The project prioritizes lean binaries and predictable performance: we use straightforward, well-understood concurrency primitives (via oneTBB) and avoid gratuitous template metaprogramming or other "clever" tricks that hurt readability and optimization. Every component is selected and configured to minimize size while retaining full HTTPS/SSH functionality, including high-speed MD5 hashing powered by OpenSSL for compatibility checks.

## Requirements

- CMake 3.26+
- Ninja build system

## Environment Constraints

All development must remain confined to this repository directory. Do not create symlinks, move system files, or otherwise modify the host environment outside `envy-cmake-test`.

## Quick Start

Iterate with link-time optimization disabled to keep rebuilds fast:

```bash
cmake -S . -B out/build -G Ninja -D CMAKE_BUILD_TYPE=Release -D ENABLE_LTO=OFF
cmake --build out/build --target envy --parallel
out/build/envy
```

The helper script `./build.sh` at the project root handles this workflow for you: it lazily configures `out/build` if the cache is missing and always drives a full build. The build is quick enough that targeting individual binaries is unnecessary.

CMake is used strictly for builds; there is no CTest integration. Run the resulting `out/build/envy` executable directly to verify integration.

## CMake Layout

- `cmake/Dependencies.cmake` is the single aggregation point. It only includes shared helpers and the modules under `cmake/deps/`; keep per-library logic out of the top-level file.
- Each vendored dependency has a corresponding module (for example `cmake/deps/Libgit2.cmake`) that owns its `FetchContent` declaration, cache knobs, and build tweaks. Add new dependencies by mirroring that pattern.
- Shared utilities such as `EnvyFetchContent.cmake` and `DependencyPatches.cmake` provide the common job pools, cache wiring, and patch helpers that modules may reuse.
- Patch scripts must be generated with `configure_file()` using templates in `cmake/templates/` so reconfigures stay idempotent and we avoid rewriting vendored sources multiple times.

Before declaring a task done, blow away the build tree and validate a clean configuration, full rebuild, and runtime verification without warnings or errors:

```bash
rm -rf out
cmake -S . -B out/build -G Ninja -D CMAKE_BUILD_TYPE=Release -D ENABLE_LTO=ON
cmake --build out/build --target envy --parallel
out/build/envy
```

Source files are formatted with 2-space indentation as enforced by the repository `.editorconfig`.

The `envy` executable resides in `out/build/envy` and exercises every dependency.

All transient artifacts must land underneath `out/`. Cache vendored source trees, extracted archives, and any vendor installs inside `out/cache/third_party`; build logic must verify the required payloads exist there before initiating another fetch so we avoid re-downloading large dependencies. Configure and build into `out/build`, which holds the CMake cache, Ninja objects, and the final binaries. Deleting `out/build` forces a rebuild while preserving the dependency cache, while removing `out/` entirely restores the repository to a pristine state.

See `AGENTS.md` for contributor guidance and `docs/dependencies.md` for notes on configuring the bundled libraries.

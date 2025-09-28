# codex-cmake-test

Static macOS test driver that bundles libgit2, libcurl (SecureTransport), libssh2, mbedTLS, Lua, oneTBB, libarchive, and BLAKE3 into a single executable using modern CMake. The goal is to build each dependency from source without Git submodules, link everything statically, and exercise the libraries in a unified smoke test.

The project prioritizes lean binaries and predictable performance: we use straightforward, well-understood concurrency primitives (via oneTBB) and avoid gratuitous template metaprogramming or other "clever" tricks that hurt readability and optimization. Every component is selected and configured to minimize size while retaining full HTTPS/SSH functionality, including high-speed MD5 hashing powered by mbedTLS for compatibility checks.

## Requirements

- CMake 3.26+
- Ninja build system

## Quick Start

```bash
cmake -S . -B out -G Ninja -D CMAKE_BUILD_TYPE=Release -D ENABLE_LTO=ON
cmake --build out --target codex_cmake_test --parallel
ctest --test-dir out -V
```

Source files are formatted with 2-space indentation as enforced by the repository `.editorconfig`.

`codex_cmake_test` resides in `out/codex-tool` and runs a runtime smoke suite across every dependency.

All third-party checkouts, build artifacts, and installs stay underneath `out/`; deleting the directory restores a pristine working tree.

See `AGENTS.md` for contributor guidance and `docs/dependencies.md` for notes on configuring the bundled libraries.

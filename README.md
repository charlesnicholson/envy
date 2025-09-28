# codex-cmake-test

Static macOS test driver that bundles libgit2, libcurl, OpenSSH, Lua, oneTBB, libarchive, and BLAKE3 into a single executable using modern CMake. The goal is to build each dependency from source without Git submodules, link everything statically, and exercise the libraries in a unified smoke test.

## Quick Start

```bash
cmake -S . -B out -D CMAKE_BUILD_TYPE=Release -D ENABLE_LTO=ON
cmake --build out --target codex_cmake_test --parallel
ctest --test-dir out -V
```

`codex_cmake_test` resides in `out/codex-tool` and runs a runtime smoke suite across every dependency.

All third-party checkouts, build artifacts, and installs stay underneath `out/`; deleting the directory restores a pristine working tree.

See `AGENTS.md` for contributor guidance and `docs/dependencies.md` for notes on configuring the bundled libraries.

NO FILES OUTSIDE THE PROJECT ROOT MAY BE TOUCHED WITHOUT EXPLICIT USER PERMISSION. NO PERMANENT CHANGES TO THE HOST ENVIRONMENT ARE EVER ALLOWED.

# Repository Guidelines

## Envy Overview
Envy is a freeform package manager orchestrated via Lua scripts—"freeform" means zero opinion on what packages represent. Authors compose packages via verbs: `fetch`, `cache`, `deploy`, `asset`, `check`, `update`. Build flows from these verbs; avoid bespoke tooling. User-wide cache serves all projects, optimized for large payloads (arm-gcc, llvm-clang, J-Link) and machine-global installs (Python, Homebrew). Deeply parallelized—preserve concurrency in all automation.

## Environment Constraints
Operate strictly within repository—no modifications outside project tree. Never alter host environment (system config, package installs, symlinks). Never modify third-party code without explicit user approval.

## Documentation Style
- All documentation should be terse and pithy. Maximize information density; minimize word count. Examples should demonstrate multiple concepts simultaneously.
- Future enhancement documents (e.g., `docs/future-enhancements.md`) should be especially concise: ~2 terse sentences and one pithy example per bullet.
- Avoid verbose explanations of trade-offs, benefits, or implementation details unless they're critical to understanding.
- Prefer short, declarative sentences. Use em-dashes and semicolons to compress related ideas. Strip filler words.

## Project Structure & Module Organization
`CMakeLists.txt` configures C++20 driver; all dependencies via `cmake/Dependencies.cmake`—no ad-hoc `FetchContent`. No Git submodules—fetch via CMake for lightweight repo. Third-party setup in `cmake/deps/<Lib>.cmake`; shared helpers in `cmake/EnvyFetchContent.cmake` or `cmake/DependencyPatches.cmake`. Patches use `configure_file()` materializing scripts in binary tree via `cmake/templates/` (idempotent reconfigures). Runtime sources in `src/`, public headers in `include/`—keep headers self-contained. Tests under `tests/` (create when needed), mirror target names (`tests/<target>_*.cpp`). Design notes in `docs/`; update `docs/dependencies.md` when pinning/patching.

## Build, Test, and Development Commands
Configuration fixed: only `CMAKE_BUILD_TYPE` and `ENABLE_LTO` vary—no additional options. Third-party tests/examples always disabled. Dev builds: `ENABLE_LTO=OFF` for speed (re-enable for release). Configure: `cmake -S . -B out/build -G Ninja -D CMAKE_BUILD_TYPE=Release -D ENABLE_LTO=ON`. Ninja only—Makefiles break isolation. Use `./build.sh` wrapper for routine work (full build, fast enough). Cache third-party in `out/cache/third_party`—check before fetch. All artifacts under `out/build` (e.g., `out/build/envy`). Delete `out/build` to force rebuild; delete `out/` for pristine tree. Build driver: `cmake --build out/build --target envy_cmake_test --parallel` (exercises libgit2, libcurl, libssh2, OpenSSL, Lua, oneTBB, libarchive, BLAKE3). Iterate on deps: `--target <dependency>` avoids reconfigure. Tasks incomplete until `./build.sh` succeeds—no warnings/errors. Third-party configures during top-level configure, builds during top-level build—never at configure-time. macOS: `otool -L out/build/envy` must show only system libs (`libz`, `libiconv`, `libSystem`, `libresolv`, `libc++`)—third-party as dynamic deps means misconfiguration.

## Coding Style & Naming Conventions
C++20, 2-space indent (`.editorconfig`), Allman braces. `CamelCase` classes, `snake_case` functions, `kPascalCase` constants. Explicit includes; `<module/Header.hpp>` style. Match public API in `include/`. `clang-format` pending—run local profile, minimize diffs.

## Testing Guidelines & Performance Philosophy
Focused tests via lightweight harnesses outside CTest—CMake builds only. Tests fast (<1s), clean allocations (`git_libgit2_shutdown`, `curl_easy_cleanup`, `archive_write_free`). Diagnose via `out/build/envy`; document repro in PR/docs. Optimize for small binaries, high runtime performance—simple cache-friendly structures, thread-aware algorithms (oneTBB), no excessive template metaprogramming. Use bundled OpenSSL for hashing (e.g., MD5) over bespoke code.

## Commit & Pull Request Guidelines
Conventional Commit headers (`feat:`, `fix:`, `build:`, `docs:`). Squash fixups locally—one commit per logical change. PRs describe dependency revisions, build command, runtime output (or rationale). Update `docs/dependencies.md` for cross-library changes; reference in PR. No backward compatibility—prioritize current needs, simplify aggressively.

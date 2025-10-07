NO FILES OUTSIDE THE PROJECT ROOT MAY BE TOUCHED WITHOUT EXPLICIT USER PERMISSION. NO PERMANENT CHANGES TO THE HOST ENVIRONMENT ARE EVER ALLOWED.

# Repository Guidelines

## Envy Overview
- Envy is a freeform package manager orchestrated via Lua scripts. “Freeform” means Envy stays unopinionated about what a package represents; authors compose packages entirely through verbs exposed in Lua.
- Core verbs presently include `fetch`, `cache`, `deploy`, `asset` (locate cached assets), `check`, and `update` (ensure a project remains current). Build new flows out of these verbs instead of bolting on bespoke tooling.
- A user-wide cache backs every invocation and can serve any number of projects under the user’s directories. It is optimized for large payloads such as arm-gcc, llvm-clang, or SEGGER J-Link bundles while still accommodating machine-global installs like Python or Homebrew on macOS.
- The system is deeply parallelized; expect concurrent package resolution and ensure new automation preserves that efficiency.

## Environment Constraints
- Operate strictly within this repository directory; do not modify files or state outside the project tree.
- Never change the host environment (no system-wide config edits, package installs, symlinks, or file moves outside the repo).

## Project Structure & Module Organization
- `CMakeLists.txt` configures the C++20 test driver and pulls in all dependencies through `cmake/Dependencies.cmake`; do not add ad-hoc `FetchContent` calls elsewhere.
- Avoid Git submodules—every vendored dependency must be fetched or mirrored through CMake so the repository remains lightweight and reproducible.
- Third-party setup lives in dedicated modules under `cmake/deps/` (e.g., `cmake/deps/OpenSSL.cmake`). Extend those files instead of editing `cmake/Dependencies.cmake` directly, and keep any shared helpers in `cmake/EnvyFetchContent.cmake` or `cmake/DependencyPatches.cmake`.
- Patch logic must use `configure_file()` to materialize scripts in the binary tree; add new templates under `cmake/templates/` and wire them through the helpers in `cmake/DependencyPatches.cmake` so repeated reconfigures stay idempotent.
- Runtime sources live in `src/` with public headers kept in `include/`; keep C++ headers self-contained so static consumers remain deterministic.
- Add test scaffolding under `tests/` (create the directory when needed). Mirror target names (`tests/<target>_*.cpp`) and share fixtures via subdirectories only when reused.
- Any design notes or per-library instructions should go under `docs/`; update `docs/dependencies.md` when pinning or patching vendored code.

## Build, Test, and Development Commands
- Configuration is intentionally fixed: only `CMAKE_BUILD_TYPE` and `ENABLE_LTO` may vary. Do **not** introduce additional cache options or expose third-party toggles.
- All vendored third-party tests and examples must remain disabled; never enable them in CI or local builds.
- For day-to-day development disable LTO for faster turns: `cmake -S . -B out/build -G Ninja -D CMAKE_BUILD_TYPE=Release -D ENABLE_LTO=OFF` (re-enable before release validation).
- `cmake -S . -B out/build -G Ninja -D CMAKE_BUILD_TYPE=Release -D ENABLE_LTO=ON` configures an out-of-tree build rooted at `out/`. Ninja is the only supported generator—avoid Makefiles so third-party downloads, installs, and object files remain isolated.
- Use the top-level `./build.sh` wrapper in the project root for routine work; it configures `out/build` on demand and always drives a full build, which is fast enough that splitting targets is unnecessary.
- Cache all fetched or generated third-party payloads inside `out/cache/third_party`. Build logic must check for the required archives or source trees in that directory before attempting a network fetch so we do not redownload large toolchains unnecessarily.
- Place every configure cache, object file, and executable under `out/build`; for example `out/build/envy` for the smoke driver. Deleting `out/build` forces a rebuild while preserving the dependency cache.
- `cmake --build out/build --target envy_cmake_test --parallel` compiles the statically linked driver that exercises libgit2, libcurl (OpenSSL), libssh2, OpenSSL, Lua, oneTBB, libarchive, and BLAKE3.
- When iterating on dependency behaviour, rebuild individual targets with `cmake --build out/build --target <dependency>` to avoid full reconfigure cycles.
- All transient artifacts must stay under `out/`, and removing the directory (`rm -rf out`) restores a pristine working tree. A task is incomplete until the full-cycle rebuild succeeds with no warnings or errors.
- Any task that modifies source code is only considered complete once `./build.sh` finishes successfully.
- Third-party CMake subprojects must configure during the top-level configure step and build during the top-level build step; never compile third-party libraries at configure-time or trigger new configuration phases from the build graph.
- On macOS, `otool -L out/build/envy` must list only system frameworks plus `libz`, `libiconv`, `libSystem`, `libresolv`, and `libc++`. If any third-party static dependency appears as a dynamic dependency, the build is misconfigured and must be corrected before shipping.

## Coding Style & Naming Conventions
- C++20, 2-space indentation (enforced by `.editorconfig`), and Allman braces for functions/namespaces. Prefer `CamelCase` classes, `snake_case` free/static functions, and `kPascalCase` constants.
- Keep transitive includes explicit; favour `<module/<Header>.hpp>` style paths and add a matching unit in `include/` for every new public API.
- `clang-format` configuration is forthcoming—run your local profile but keep diffs minimal until the repo-standard file lands.

## Testing Guidelines & Performance Philosophy
- Add focused tests through lightweight harnesses that run outside CTest; CMake should remain build-only.
- Runtime checks should remain fast (<1s) and must clean up allocations (e.g., `git_libgit2_shutdown`, `curl_easy_cleanup`, `archive_write_free`).
- For diagnosis, invoke `out/build/envy` directly and document nuanced repro steps in PR descriptions or `docs/`.
- Optimize for small binaries and high runtime performance. Favor simple, cache-friendly data structures, thread-aware algorithms (via oneTBB), and avoid "cute" or excessive template metaprogramming that complicates maintenance without measurable benefit. Static hashing needs (e.g., MD5) should lean on the bundled OpenSSL implementation rather than bespoke code.

## Commit & Pull Request Guidelines
- Use Conventional Commit headers (`feat:`, `fix:`, `build:`, `docs:`) so changelog automation remains straightforward.
- Squash iterative fixups locally; keep one commit per logical change (e.g., "build: pin libarchive to 3.7.2").
- Every PR must describe dependency revisions touched, the build command used, and include the relevant runtime output (or rationale when infeasible).
- Coordinate cross-library adjustments by updating `docs/dependencies.md` and referencing the section in your PR summary for easy reviewer context.
- Do not preserve backward compatibility; prioritize the current requirements and simplify aggressively when older entry points get in the way.

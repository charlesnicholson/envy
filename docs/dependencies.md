# Dependency Notes

This repository is an experiment in linking several complex dependencies statically on macOS. The top-level `cmake/Dependencies.cmake` feeds `FetchContent` and `ExternalProject` with pinned revisions so we can build from clean source checkouts without Git submodules.

All downloads, source trees, and install steps are redirected underneath the active build directory (recommended: `out/`). Removing that directory cleans every third-party artifact.

## libgit2
- Source: https://github.com/libgit2/libgit2 (tag `v1.7.2`).
- CMake options disable CLI and shared builds. The library exports as `codex::libgit2` and is consumed via the standard `git2` target.

## libcurl
- Source: https://github.com/curl/curl (tag `curl-8_8_0`).
- Compiled with OpenSSL and zlib support; CURL executable is disabled. For static builds we set `CURL_STATICLIB` and create the `CURL::libcurl` alias.

## OpenSSH
- Source: https://github.com/openssh/openssh-portable (tag `V_9_7_P1`).
- Built via `ExternalProject_Add`, requesting a static `libssh.a` and installing into `${build}/third_party/openssh` (with `${build}` usually `out/`). The imported target is published as `openssh::ssh`.
- Expect to provide the macOS SDK `sdkroot` and `--with-security-key-builtin` flags when touching hardware-backed authentication.
- Headers are consumed as `<libssh/libssh.h>`; confirm the install step places headers under `${prefix}/include/libssh` after configuration changes.

## Lua
- Source: https://github.com/lua/lua (tag `v5.4.6`).
- Upstream Makefile is bypassed; we build `lua` as a static library directly with `add_library` and expose it as `lua::lua`. The helper header `include/lua.hpp` wraps the C headers for seamless C++ consumption.

## oneTBB
- Source: https://github.com/oneapi-src/oneTBB (tag `v2022.0.0`).
- Builds via upstream CMake with tests off. We link against `TBB::tbb` and keep `BUILD_SHARED_LIBS=OFF` for static archives.

## libarchive
- Source: https://github.com/libarchive/libarchive (tag `v3.7.2`).
- Non-essential tools and compression backends are disabled so the build depends only on project-managed sources. The `archive` target is re-exported as `libarchive::libarchive` for consumers.
- Keep changes to compression feature flags synchronized with `check_libarchive()` in `src/main.cpp` so the runtime probe reflects the configured capabilities.

## BLAKE3
- Source: https://github.com/BLAKE3-team/BLAKE3 (tag `1.5.1`).
- The C implementation is built directly with architecture-specific files added conditionally (x86-64 SIMD or ARM NEON). The exported target is `blake3::blake3` with headers under the `c/` directory.
- Update the smoke tests if you adjust SIMD availability so we continue to hash test vectors along the most optimized path.

## Runtime Probe
`src/main.cpp` exercises each dependency in isolation to verify compile-time and runtime integration. The executable is meant as a smoke test and should remain fast (<1s) so we can re-run it during development and in CI.

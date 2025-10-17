# Dependency Notes

This repository is an experiment in linking several complex dependencies statically on macOS. The top-level `cmake/Dependencies.cmake` feeds `FetchContent` and `ExternalProject` with pinned revisions so we can build from clean source checkouts without Git submodules.

All downloads, source trees, and install steps are redirected underneath the active build directory (recommended: `out/`). Removing that directory cleans every third-party artifact.

## Build Workflow
- `./build.sh` is the supported entry point and always drives the `release-lto-on` preset so local and IDE workflows stay aligned.
- The preset keeps LTO enabled by default; run `cmake --preset release-lto-on` directly if you need manual control.
- Presets share the fixed binary directory `out/build` and reuse the dependency cache under `out/cache` for predictable rebuilds.
- Set `ENVY_FETCH_FULLY_DISCONNECTED=ON` before running `./build.sh` to skip network checks and force reuse of cached third-party sources.
- When adjusting third-party wiring, add helper scripts under `cmake/scripts/` so `cmake/Dependencies.cmake` remains declarative.

## libgit2
- Source: https://github.com/libgit2/libgit2 (tag `v1.9.1`).
- CMake options disable CLI and shared builds. The library exports as `envy::libgit2` and is consumed via the standard `git2` target.
- `USE_HTTPS` selects WinHTTP on Windows and mbedTLS elsewhere so HTTPS rides the platform-preferred backend while SSH flows through libssh2.
- Patched `FindStatNsec.cmake` forces `_POSIX_C_SOURCE=200809L`/`_GNU_SOURCE` during feature probes so nanosecond detection stays lit on glibc toolchains.

## libcurl
- Source: https://github.com/curl/curl (tag `curl-8_16_0`).
- Built with WinSSL/SChannel on Windows and mbedTLS elsewhere, with libssh2 and zlib enabled. The CLI is disabled; static consumers pick up the library via `CURL::libcurl` with `CURL_STATICLIB` defined so the TLS backend matches the rest of the project.

## libssh2
- Source: https://github.com/libssh2/libssh2 (tag `libssh2-1.11.1`).
- Compiled as a static library with mbedTLS providing the cryptography backend and zlib compression enabled. The build exports as `libssh2::libssh2` and feeds both libgit2 and libcurl to provide SSH transport capabilities.

## mbedTLS
- Source: https://github.com/Mbed-TLS/mbedtls (tag `mbedtls-3.6.4`, easy-make archive).
- Built via upstream CMake with tests/programs off; `envy_mbedtls_user_config.h` enables TLS 1.3 and smooths C89 consumers before exporting the static triplet.
- A custom find-module surfaces the bundled build to libcurl and libssh2 so every TLS consumer links the same toolchain.

## Lua
- Source: https://github.com/lua/lua (tag `v5.4.8`).
- Upstream Makefile is bypassed; we build `lua` as a static library directly with `add_library` and expose it as `lua::lua`. The application includes `lua.h`, `lauxlib.h`, and `lualib.h` directly from the embedded source tree.

## oneTBB
- Source: https://github.com/oneapi-src/oneTBB (tag `v2022.2.0`).
- Builds via upstream CMake with tests off. We link against `TBB::tbb` and keep `BUILD_SHARED_LIBS=OFF` for static archives.

## AWS SDK
- Source: https://github.com/aws/aws-sdk-cpp/archive/refs/tags/1.11.661.zip.
- Built as static libraries with tests, CLI utilities, and non-essential components disabled so only `s3`, `sso`, and `sso-oidc` ship in the final bundle.
- CRT dependencies reuse AWS’s `prefetch_crt_dependency.sh`; our CMake prefetcher parses that manifest, downloads via `file(DOWNLOAD)`, and stages into `crt/aws-crt-cpp` so macOS/Linux/Windows share the same flow.
- `USE_OPENSSL` stays `OFF`—the SDK links against bundled AWS-LC so we avoid platform OpenSSL hunts, especially on MSVC.

## libarchive
- Source: https://github.com/libarchive/libarchive (tag `v3.8.1`).
- Non-essential tools and compression backends are disabled so the build depends only on project-managed sources. The `archive` target is re-exported as `libarchive::libarchive` for consumers.
- Keep changes to compression feature flags synchronized with `check_libarchive()` in `src/main.cpp` so the runtime probe reflects the configured capabilities.
- Patch forces `sys/types.h` onto libarchive’s type-size probes so glibc exposes `id_t`/friends and config.h skips redefining them.

## BLAKE3
- Source: https://github.com/BLAKE3-team/BLAKE3 (tag `1.8.2`).
- The C implementation is built directly with architecture-specific files added conditionally (x86-64 SIMD or ARM NEON). The exported target is `blake3::blake3` with headers under the `c/` directory.
- Update the runtime checks if you adjust SIMD availability so we continue to hash test vectors along the most optimized path.

## zlib
- Source: https://zlib.net/zlib-1.3.1.tar.gz.
- Built statically with upstream CMake—examples/tests off—and exported as `ZLIB::ZLIB` so libgit2, libssh2, and libcurl consume the same archive across every platform.

## Runtime Probe
`src/main.cpp` exercises each dependency in isolation to verify compile-time and runtime integration. The executable should remain fast (<1s) so we can re-run it during development and in CI.

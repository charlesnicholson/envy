# Dependency Notes

This repository is an experiment in linking several complex dependencies statically on macOS. The top-level `cmake/Dependencies.cmake` feeds `FetchContent` and `ExternalProject` with pinned revisions so we can build from clean source checkouts without Git submodules.

All downloads, source trees, and install steps are redirected underneath the active build directory (recommended: `out/`). Removing that directory cleans every third-party artifact.

## Build Workflow
- `./build.sh` is the supported entry point and always drives the `release-lto-on` preset so local and IDE workflows stay aligned.
- The preset keeps LTO enabled by default; run `cmake --preset release-lto-on` directly if you need manual control.
- Presets share the fixed binary directory `out/build` and reuse the dependency cache under `out/cache/third_party` for predictable rebuilds.
- Set `ENVY_FETCH_FULLY_DISCONNECTED=ON` before running `./build.sh` to skip network checks and force reuse of cached third-party sources.
- When adjusting third-party wiring, add helper scripts under `cmake/scripts/` so `cmake/Dependencies.cmake` remains declarative.

## libgit2
- Source: https://github.com/libgit2/libgit2 (tag `v1.9.1`).
- CMake options disable CLI and shared builds. The library exports as `envy::libgit2` and is consumed via the standard `git2` target.
- `USE_HTTPS` is forced to `SecureTransport` and `USE_SSH` is enabled so HTTPS traffic rides the system TLS stack while SSH uses our bundled libssh2 build.

## libcurl
- Source: https://github.com/curl/curl (tag `curl-8_16_0`).
- Built with OpenSSL (using the in-tree static toolchain) and libssh2 enabled alongside zlib; the curl CLI is disabled. Static consumers pick up the library via the `CURL::libcurl` alias with `CURL_STATICLIB` defined, and the TLS backend now aligns with the rest of the project.
- On macOS the build points `CURL_CA_BUNDLE` at `/etc/ssl/cert.pem` so OpenSSL inherits the system trust store; adjust the corresponding cache entry if another path is required on your platform.

## libssh2
- Source: https://github.com/libssh2/libssh2 (tag `libssh2-1.11.1`).
- Compiled as a static library with OpenSSL providing the cryptography backend and zlib compression enabled. The build exports as `libssh2::libssh2` and feeds both libgit2 and libcurl to provide SSH transport capabilities.

## OpenSSL
- Source: https://www.openssl.org/source/openssl-3.6.0.tar.gz.
- Built via `ExternalProject_Add` invoking the upstream Configure script with `no-shared`, `no-tests`, and `no-apps` so we export static `OpenSSL::SSL`/`OpenSSL::Crypto` targets without shipping the CLI tooling. The install lands inside the build tree and a generated `OpenSSLConfig.cmake` allows other dependencies (libssh2) to `find_package` the bundled build.
- The runtime probes use OpenSSL's `MD5` implementation to validate a known digest while also confirming TLS 1.3-capable libraries are present for consumers such as libssh2.

## Lua
- Source: https://github.com/lua/lua (tag `v5.4.8`).
- Upstream Makefile is bypassed; we build `lua` as a static library directly with `add_library` and expose it as `lua::lua`. The helper header `include/lua.hpp` wraps the C headers for seamless C++ consumption.

## oneTBB
- Source: https://github.com/oneapi-src/oneTBB (tag `v2022.2.0`).
- Builds via upstream CMake with tests off. We link against `TBB::tbb` and keep `BUILD_SHARED_LIBS=OFF` for static archives.

## AWS SDK
- Source: https://github.com/aws/aws-sdk-cpp/archive/refs/tags/1.11.661.zip.
- Built as static libraries with tests, CLI utilities, and non-essential components disabled so only `s3`, `sso`, and `sso-oidc` ship in the final bundle.
- CRT dependencies are prefetched through `cmake/scripts/prefetch_aws_crt.cmake`, which adds the repo shims to `PATH` before invoking the upstream `prefetch_crt_dependency.sh`; reuse that script when updating SDK or CRT revisions.

## libarchive
- Source: https://github.com/libarchive/libarchive (tag `v3.8.1`).
- Non-essential tools and compression backends are disabled so the build depends only on project-managed sources. The `archive` target is re-exported as `libarchive::libarchive` for consumers.
- Keep changes to compression feature flags synchronized with `check_libarchive()` in `src/main.cpp` so the runtime probe reflects the configured capabilities.

## BLAKE3
- Source: https://github.com/BLAKE3-team/BLAKE3 (tag `1.8.2`).
- The C implementation is built directly with architecture-specific files added conditionally (x86-64 SIMD or ARM NEON). The exported target is `blake3::blake3` with headers under the `c/` directory.
- Update the runtime checks if you adjust SIMD availability so we continue to hash test vectors along the most optimized path.

## Runtime Probe
`src/main.cpp` exercises each dependency in isolation to verify compile-time and runtime integration. The executable should remain fast (<1s) so we can re-run it during development and in CI.

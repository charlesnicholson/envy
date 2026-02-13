# Windows libcurl Deprecation Plan

Replace libcurl with WinINet on Windows to reduce binary size (~300-500KB) and build complexity.

## Background

libcurl is used solely for HTTP/HTTPS/FTP/FTPS downloads (`libcurl_util.cpp`). On Windows, it links Schannel for TLS (not mbedTLS). WinINet (`wininet.dll`) provides equivalent functionality as a system DLL—zero binary impact.

**Why WinINet over WinHTTP:** WinHTTP lacks FTP support. WinINet supports HTTP, HTTPS, FTP, FTPS. Both ship with every Windows version since 2000; compatibility is a non-issue.

## Current Architecture

```
fetch.cpp
  └─> libcurl_util.cpp (libcurl_download)
        └─> curl_easy_* APIs

cmd_version.cpp
  └─> curl_version_info() (version display)
```

## Target Architecture

Platform-specific implementations behind a common header:

```
fetch.cpp
  └─> fetch_http.h (fetch_http_download)
        ├─> fetch_http_curl.cpp  (libcurl)   [Linux/macOS]
        └─> fetch_http_win32.cpp (WinINet)   [Windows]

cmd_version.cpp
  └─> #if !defined(_WIN32): curl_version_info()
       #else: "WinINet (system)" string
```

## Interface

`src/fetch_http.h` — 1:1 match with current `libcurl_download()` signature:
```cpp
#pragma once

#include "fetch.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace envy {

std::filesystem::path fetch_http_download(
    std::string_view url,
    std::filesystem::path const &destination,
    fetch_progress_cb_t const &progress,
    std::optional<std::string> const &post_data);

}  // namespace envy
```

No `fetch_http_init()`—both backends use lazy or no-op initialization internally.

## Implementation Plan

### 1. Create `src/fetch_http.h`

Platform-agnostic header as shown above. Preserves `post_data` parameter (WinINet supports POST via `HttpSendRequest`).

### 2. Create `src/fetch_http_curl.cpp`

Rename/refactor `libcurl_util.cpp`:
- Implement `fetch_http_download()` using libcurl (keep existing lazy `std::once_flag` init)
- Guard with `#if !defined(_WIN32)`
- Named `_curl` not `_posix`—macOS uses system libcurl, Linux builds from source; both are curl backends

### 3. Create `src/fetch_http_win32.cpp`

Implement `fetch_http_download()` using WinINet. Guard with `#if defined(_WIN32)`.

**Session handling:** Process-wide singleton via `std::once_flag`—created on first download, reused for all subsequent calls. Avoids repeated WPAD proxy auto-detection that serializes concurrent downloads.

**Download flow:**
- `InternetOpen` — session with `INTERNET_OPEN_TYPE_PRECONFIG` (respects system proxy)
- For HTTP/HTTPS with POST: `InternetConnect` + `HttpOpenRequest` + `HttpSendRequest` with body
- For GET/FTP: `InternetOpenUrl` (handles redirects automatically)
- `HttpQueryInfo` — query `Content-Length` for progress total
- `InternetReadFile` loop — read chunks, write to `std::ofstream`, fire progress callback with running totals
- `InternetCloseHandle` — cleanup all handles

**Progress:** Driven from `InternetReadFile` loop (simpler and more reliable than `InternetSetStatusCallback`). Query `Content-Length` upfront for total; accumulate bytes read for transferred. Maps directly to `fetch_transfer_progress`.

**Flags:**
- `INTERNET_FLAG_RELOAD` — bypass WinINet cache
- `INTERNET_FLAG_NO_CACHE_WRITE` — don't cache response
- `INTERNET_FLAG_SECURE` — automatic for https:// URLs

**POST support:** Use `InternetConnect` + `HttpOpenRequest("POST")` + `HttpSendRequest(body)` instead of `InternetOpenUrl`.

**Error handling:** `GetLastError()` after failures; `FormatMessage` for readable errors; `InternetGetLastResponseInfo` for FTP server errors.

### 4. Update Callers

**`src/fetch.cpp`:** Replace `#include "libcurl_util.h"` with `#include "fetch_http.h"`; rename `libcurl_download()` → `fetch_http_download()`.

**`src/cmds/cmd_version.cpp`:** Wrap curl version block in `#if !defined(_WIN32)`. On Windows, print `"WinINet (system)"` instead.

### 5. Update CMake

**`cmake/deps/Libcurl.cmake`:** Wrap entire file body in `if(NOT WIN32)`:
```cmake
if(WIN32)
    return()
endif()
# ... existing content unchanged ...
```

**`cmake/Dependencies.cmake`:**
- Conditional `CURL::libcurl` link (non-Windows only)
- Add `wininet` to `PLATFORM_NETWORK_LIBS` on Windows
- Conditional `CURL_STATICLIB` define (non-Windows, non-Darwin)
- Conditional curl include directories (non-Windows, non-Darwin)

**`CMakeLists.txt`:**
- Replace unconditional `src/libcurl_util.cpp` with platform-conditional sources:
  - Windows: `src/fetch_http_win32.cpp`
  - Non-Windows: `src/fetch_http_curl.cpp`
- Conditional libcurl license source (non-Windows, non-Apple)

### 6. Delete Old Files

- `src/libcurl_util.h`
- `src/libcurl_util.cpp`

### 7. Test

Build and verify on Windows (WinINet path). CI validates macOS/Linux (curl path).

| Protocol  | Test |
|-----------|------|
| HTTP      | Download from http:// URL |
| HTTPS     | Download from https:// URL |
| FTP       | Download from ftp:// URL |
| Redirects | HTTP 301/302 followed automatically |
| Progress  | Callback invoked during transfer |
| Errors    | Invalid URL, network failure, 404 |
| POST      | POST with body (if exercised) |

### 8. Cleanup

- Update `docs/dependencies.md` to note platform-specific HTTP backends
- Verify Windows binary has no curl symbols
- Verify macOS/Linux still link libcurl correctly

## Custom CA Certificates

**Windows (WinINet):** Uses Windows Certificate Store automatically. Corporate CAs deployed via Group Policy work with no app configuration—an improvement over libcurl.

**POSIX (libcurl+mbedTLS):** mbedTLS doesn't auto-discover system CA stores. Stock HTTPS works (libcurl finds default CA bundle), but custom corporate CA chains are untested. May require `CURLOPT_CAINFO` or respecting `SSL_CERT_FILE` / `CURL_CA_BUNDLE` env vars. Needs investigation if enterprise POSIX support becomes a requirement.

## References

- [WinINet Functions](https://learn.microsoft.com/en-us/windows/win32/wininet/wininet-functions)
- [InternetOpenUrl](https://learn.microsoft.com/en-us/windows/win32/api/wininet/nf-wininet-internetopenurla)
- [HttpSendRequest](https://learn.microsoft.com/en-us/windows/win32/api/wininet/nf-wininet-httpsendrequesta)
- [InternetReadFile](https://learn.microsoft.com/en-us/windows/win32/api/wininet/nf-wininet-internetreadfile)

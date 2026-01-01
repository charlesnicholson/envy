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
```

## Target Architecture

Create a platform-agnostic `fetch_http` module with platform-specific implementations:

```
fetch.cpp
  └─> fetch_http.h (fetch_http_download)
        ├─> fetch_http_posix.cpp (libcurl)   [Linux/macOS]
        └─> fetch_http_win32.cpp (WinINet)   [Windows]
```

No files named "curl" or "libcurl"—some builds won't use curl.

## Interface

`src/fetch_http.h`:
```cpp
#pragma once

#include "fetch.h"
#include <filesystem>
#include <string_view>

namespace envy {

void fetch_http_init();  // call once at startup

std::filesystem::path fetch_http_download(
    std::string_view url,
    std::filesystem::path const &destination,
    fetch_progress_cb_t const &progress);

}
```

Features required: URL download to file, follow redirects, progress callbacks, error handling.

## Implementation Plan

### 1. Create `src/fetch_http.h`

Platform-agnostic header as shown above.

### 2. Create `src/fetch_http_posix.cpp`

Rename/refactor `libcurl_util.cpp`:
- Implement `fetch_http_download()` using libcurl
- `fetch_http_init()` calls `curl_global_init()`
- Wrap with `#if !defined(_WIN32)`

Key libcurl calls: `curl_easy_init`, `curl_easy_setopt`, `curl_easy_perform`, `curl_easy_cleanup`.

### 3. Create `src/fetch_http_win32.cpp`

Implement `fetch_http_download()` using WinINet:

- `InternetOpen` — initialize session (user agent, proxy settings)
- `InternetOpenUrl` — open HTTP/HTTPS/FTP URL (handles redirects automatically)
- `InternetReadFile` — read response body in chunks, write to `std::ofstream`
- `InternetSetStatusCallback` — progress notifications via `INTERNET_STATUS_CALLBACK`
- `InternetCloseHandle` — cleanup
- `fetch_http_init()` — no-op (WinINet needs no global init)

Key WinINet flags:
- `INTERNET_FLAG_RELOAD` — bypass cache
- `INTERNET_FLAG_NO_CACHE_WRITE` — don't cache response
- `INTERNET_FLAG_SECURE` — HTTPS (automatic for https:// URLs)

Error handling: `GetLastError()` after failures; `InternetGetLastResponseInfo` for FTP errors.

Wrap with `#if defined(_WIN32)`.

### 4. Update CMake

```cmake
target_sources(envy PRIVATE src/fetch_http.h)

if(WIN32)
    target_sources(envy PRIVATE src/fetch_http_win32.cpp)
    target_link_libraries(envy PRIVATE wininet)
else()
    target_sources(envy PRIVATE src/fetch_http_posix.cpp)
    target_link_libraries(envy PRIVATE CURL::libcurl)
endif()
```

Remove libcurl from Windows build entirely in `cmake/deps/Libcurl.cmake`:
```cmake
if(NOT WIN32)
    # existing libcurl FetchContent setup
endif()
```

### 5. Update Callers

In `fetch.cpp`, replace:
```cpp
#include "libcurl_util.h"
// libcurl_download(...)
```

With:
```cpp
#include "fetch_http.h"
// fetch_http_download(...)
```

Call `fetch_http_init()` from `main()` or lazy-init within the module.

### 6. Delete Old Files

- `src/libcurl_util.h`
- `src/libcurl_util.cpp`

### 7. Test Matrix

| Protocol | Test |
|----------|------|
| HTTP | Download from http:// URL |
| HTTPS | Download from https:// URL |
| FTP | Download from ftp:// URL |
| FTPS | Download from ftps:// URL (if needed) |
| Redirects | HTTP 301/302 followed automatically |
| Progress | Callback invoked during transfer |
| Errors | Invalid URL, network failure, 404 |

Run full test suite on Windows, macOS, and Linux.

### 8. Cleanup

- Update `docs/dependencies.md` to note platform-specific HTTP backends
- Verify Windows binary has no curl symbols
- Verify macOS/Linux still link libcurl correctly

## WinINet Progress Callback

```cpp
void CALLBACK progress_callback(
    HINTERNET hInternet,
    DWORD_PTR dwContext,      // user data pointer
    DWORD dwInternetStatus,   // status code
    LPVOID lpvStatusInfo,
    DWORD dwStatusInfoLength);
```

Relevant status codes:
- `INTERNET_STATUS_RECEIVING_RESPONSE` — starting receive
- `INTERNET_STATUS_RESPONSE_RECEIVED` — bytes received (cast `lpvStatusInfo` to `DWORD*`)

For total size: query `Content-Length` header via `HttpQueryInfo` after `InternetOpenUrl`.

## References

- [WinINet Functions](https://learn.microsoft.com/en-us/windows/win32/wininet/wininet-functions)
- [InternetOpenUrl](https://learn.microsoft.com/en-us/windows/win32/api/wininet/nf-wininet-internetopenurla)
- [InternetSetStatusCallback](https://learn.microsoft.com/en-us/windows/win32/api/wininet/nf-wininet-internetsetstatuscallback)

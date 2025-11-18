#pragma once

#include "util.h"

#ifdef _WIN32

#include "platform_windows.h"

namespace envy {

// RAII wrapper for mbedtls threading initialization/cleanup on Windows.
// Uses SRWLOCK-based MBEDTLS_THREADING_ALT implementation.
struct mbedtls_threading_scope : unmovable {
  mbedtls_threading_scope();
  ~mbedtls_threading_scope();
};

}  // namespace envy

#endif  // _WIN32

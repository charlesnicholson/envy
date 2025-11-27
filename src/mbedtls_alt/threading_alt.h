#pragma once

#ifndef _WIN32
#error "threading_alt.h should only be used on Windows with MBEDTLS_THREADING_ALT"
#endif

#include "../platform_windows.h"

// SRWLOCK-based mutex for mbedtls non-recursive THREADING_ALT on Windows.
typedef struct {
  SRWLOCK lock;
} mbedtls_threading_mutex_t;

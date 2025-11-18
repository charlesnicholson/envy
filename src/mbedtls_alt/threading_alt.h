#pragma once

#ifdef _WIN32

// Ensure proper Windows header order for winsock2
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>
#include <windows.h>

// SRWLOCK-based mutex for mbedtls THREADING_ALT on Windows.
// mbedtls expects non-recursive mutexes; SRWLOCK is ideal.
typedef struct {
    SRWLOCK lock;
} mbedtls_threading_mutex_t;

#else
#error "threading_alt.h should only be used on Windows with MBEDTLS_THREADING_ALT"
#endif

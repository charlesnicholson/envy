#ifndef _WIN32
#error "mbedtls_threading_windows.cpp should only be compiled on Windows"
#endif

#include "mbedtls_threading_windows.h"

#include "platform_windows.h"

#include <mbedtls/threading.h>

namespace envy {

// SRWLOCK-based mutex for mbedtls THREADING_ALT.
// mbedtls expects non-recursive mutexes; SRWLOCK is ideal (lighter than CRITICAL_SECTION).
static_assert(sizeof(mbedtls_threading_mutex_t) == sizeof(SRWLOCK),
              "mbedtls_threading_mutex_t must match SRWLOCK size");

static void mutex_init_srwlock(mbedtls_threading_mutex_t *mutex) {
  if (mutex == nullptr) { return; }
  InitializeSRWLock(&mutex->lock);
}

static void mutex_free_srwlock(mbedtls_threading_mutex_t *mutex) {
  // SRWLOCK has no cleanup - no-op
  (void)mutex;
}

static int mutex_lock_srwlock(mbedtls_threading_mutex_t *mutex) {
  if (mutex == nullptr) { return MBEDTLS_ERR_THREADING_BAD_INPUT_DATA; }
  AcquireSRWLockExclusive(&mutex->lock);
  return 0;
}

static int mutex_unlock_srwlock(mbedtls_threading_mutex_t *mutex) {
  if (mutex == nullptr) { return MBEDTLS_ERR_THREADING_BAD_INPUT_DATA; }
  ReleaseSRWLockExclusive(&mutex->lock);
  return 0;
}

mbedtls_threading_scope::mbedtls_threading_scope() {
  mbedtls_threading_set_alt(mutex_init_srwlock,
                             mutex_free_srwlock,
                             mutex_lock_srwlock,
                             mutex_unlock_srwlock);
}

mbedtls_threading_scope::~mbedtls_threading_scope() {
  mbedtls_threading_free_alt();
}

}  // namespace envy

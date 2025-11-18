#pragma once
#ifdef _WIN32

#include "platform_windows.h"
#include "util.h"

namespace envy {

struct mbedtls_threading_scope : unmovable {
  mbedtls_threading_scope();
  ~mbedtls_threading_scope();
};

}  // namespace envy

#endif  // _WIN32

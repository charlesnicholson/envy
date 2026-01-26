#pragma once

#include "util.h"

namespace envy {

// RAII wrapper for libgit2 global initialization/shutdown.
struct libgit2_scope : unmovable {
  libgit2_scope();
  ~libgit2_scope();
};

// On Linux and macOS, throws if SSL certificates required for HTTPS git
// operations are not found (no-op on Windows). Call before HTTPS git fetches.
void libgit2_require_ssl_certs();

}  // namespace envy

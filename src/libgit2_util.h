#pragma once

#include "util.h"

namespace envy {

// RAII wrapper for libgit2 global initialization/shutdown.
struct libgit2_scope : unmovable {
  libgit2_scope();
  ~libgit2_scope();
};

// Throws if SSL certificates are required but not found (Linux only).
// Call before any HTTPS git operations.
void libgit2_require_ssl_certs();

}  // namespace envy

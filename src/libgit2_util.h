#pragma once

#include "util.h"

namespace envy {

// RAII wrapper for libgit2 global initialization/shutdown.
struct libgit2_scope : unmovable {
  libgit2_scope();
  ~libgit2_scope();
};

}  // namespace envy

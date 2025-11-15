#include "libgit2_util.h"

#include <git2.h>

namespace envy {

libgit2_scope::libgit2_scope() { git_libgit2_init(); }
libgit2_scope::~libgit2_scope() { git_libgit2_shutdown(); }

}  // namespace envy

#pragma once

#include "engine.h"
#include "recipe.h"
#include "util.h"

namespace envy {

// Thread-local phase context storage for envy.* functions that need engine/recipe access.
// Each worker thread maintains its own context pointer via thread_local storage.
// Context is set by phase_context_guard RAII object during phase execution.

engine *lua_phase_context_get_engine();
recipe *lua_phase_context_get_recipe();

// RAII guard that sets thread-local context for phase execution scope.
// Automatically clears context on destruction (including exception unwinding).
class phase_context_guard : unmovable {
public:
  phase_context_guard(engine *eng, recipe *r);
  ~phase_context_guard();
};

}  // namespace envy

#pragma once

#include "engine.h"
#include "recipe.h"
#include "util.h"

#include <filesystem>

namespace envy {

// Thread-local phase context storage for envy.* functions that need engine/recipe access.
// Each worker thread maintains its own context pointer via thread_local storage.
// Context is set by phase_context_guard RAII object during phase execution.

engine *lua_phase_context_get_engine();
recipe *lua_phase_context_get_recipe();

// Phase-specific directory accessors (nullptr if not in appropriate phase)
std::filesystem::path const *lua_phase_context_get_tmp_dir();
std::filesystem::path const *lua_phase_context_get_fetch_dir();

// RAII guard that sets thread-local context for phase execution scope.
// Automatically clears context on destruction (including exception unwinding).
class phase_context_guard : unmovable {
 public:
  phase_context_guard(engine *eng, recipe *r);
  ~phase_context_guard();

  // Set phase-specific directories (call after construction)
  void set_tmp_dir(std::filesystem::path const *dir);
  void set_fetch_dir(std::filesystem::path const *dir);
};

}  // namespace envy

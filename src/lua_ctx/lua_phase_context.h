#pragma once

#include "cache.h"
#include "util.h"

#include "sol/sol.hpp"

#include <filesystem>
#include <optional>

namespace envy {

// Forward declarations
class engine;
struct pkg;

// Phase execution context - all state available to envy.* functions during phase
// execution. Stored in Lua registry, accessed via sol::this_state.
struct phase_context {
  engine *eng;
  pkg *p;
  std::optional<std::filesystem::path> run_dir;  // default cwd for envy.run()

  // May not be the same as p->lock: "custom fetch" runs with child package's lock!
  cache::scoped_entry_lock const *lock;
};

// Get phase context from Lua registry (returns nullptr if not in phase execution)
phase_context const *lua_phase_context_get(sol::this_state ts);

// RAII guard that sets Lua registry context for phase execution scope.
// Automatically clears context on destruction (including exception unwinding).
// The guard owns the context struct on the stack; registry stores pointer to it.
class phase_context_guard : unmovable {
 public:
  phase_context_guard(engine *eng,
                      pkg *p,
                      std::optional<std::filesystem::path> run_dir = std::nullopt,
                      cache::scoped_entry_lock const *lock = nullptr);
  ~phase_context_guard();

 private:
  lua_State *lua_state_;
  phase_context ctx_;
};

}  // namespace envy

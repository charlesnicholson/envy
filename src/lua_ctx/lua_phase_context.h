#pragma once

#include "util.h"

#include "sol/sol.hpp"

#include <filesystem>
#include <optional>

namespace envy {

// Forward declarations
class engine;
struct recipe;

// Lua registry-based phase context storage for envy.* functions.
// Context is stored in the Lua state's registry, accessed via sol::this_state.
// Context is set by phase_context_guard RAII object during phase execution.

// Get engine/recipe from Lua registry (returns nullptr if not set)
engine *lua_phase_context_get_engine(lua_State *L);
recipe *lua_phase_context_get_recipe(lua_State *L);

// Get the run_dir for the current phase (the default cwd for envy.run())
std::optional<std::filesystem::path> lua_phase_context_get_run_dir(lua_State *L);

// Convenience overloads for sol::this_state
inline engine *lua_phase_context_get_engine(sol::this_state ts) {
  return lua_phase_context_get_engine(ts.L);
}
inline recipe *lua_phase_context_get_recipe(sol::this_state ts) {
  return lua_phase_context_get_recipe(ts.L);
}
inline std::optional<std::filesystem::path> lua_phase_context_get_run_dir(sol::this_state ts) {
  return lua_phase_context_get_run_dir(ts.L);
}

// RAII guard that sets Lua registry context for phase execution scope.
// Automatically clears context on destruction (including exception unwinding).
class phase_context_guard : unmovable {
 public:
  // run_dir: the default cwd for envy.run() calls in this phase
  phase_context_guard(engine *eng,
                      recipe *r,
                      std::optional<std::filesystem::path> run_dir = std::nullopt);
  ~phase_context_guard();

 private:
  lua_State *lua_state_;
};

}  // namespace envy

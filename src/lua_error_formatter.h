#pragma once

#include "recipe.h"

#include "sol/forward.hpp"

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace envy {

struct lua_error_context {
  std::string lua_error_message;  // From sol::error::what()
  recipe const *r;                // For all context (spec, paths)
  std::string_view phase;         // "fetch", "build", "check", etc.
};

// Format enriched Lua error message with full context
std::string format_lua_error(lua_error_context const &ctx);

// Call a Lua function with enriched error handling
// Usage:
//   call_lua_function_with_enriched_errors(recipe, "build", [&]() {
//     return build_func(ctx_table, opts);
//   });
template <typename Callable>
auto call_lua_function_with_enriched_errors(recipe const *r,
                                            std::string_view phase,
                                            Callable &&callable) {
  sol::protected_function_result result{ std::forward<Callable>(callable)() };

  if (!result.valid()) {
    sol::error err = result;
    lua_error_context ctx{ .lua_error_message = err.what(), .r = r, .phase = phase };
    throw std::runtime_error(format_lua_error(ctx));
  }

  return result;
}

}  // namespace envy

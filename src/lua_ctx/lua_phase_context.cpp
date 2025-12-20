#include "lua_phase_context.h"

#include "lua_envy.h"
#include "recipe.h"

extern "C" {
#include "lua.h"
}

namespace envy {
namespace {

// Registry key for run_dir (phase working directory for envy.run() default cwd)
constexpr int ENVY_RUN_DIR_RIDX = 103;

}  // namespace

engine *lua_phase_context_get_engine(lua_State *L) {
  sol::state_view lua{ L };
  sol::object obj{ lua.registry()[ENVY_ENGINE_RIDX] };
  if (!obj.valid() || obj.get_type() == sol::type::lua_nil) { return nullptr; }
  return static_cast<engine *>(obj.as<void *>());
}

recipe *lua_phase_context_get_recipe(lua_State *L) {
  sol::state_view lua{ L };
  sol::object obj{ lua.registry()[ENVY_RECIPE_RIDX] };
  if (!obj.valid() || obj.get_type() == sol::type::lua_nil) { return nullptr; }
  return static_cast<recipe *>(obj.as<void *>());
}

std::optional<std::filesystem::path> lua_phase_context_get_run_dir(lua_State *L) {
  sol::state_view lua{ L };
  sol::object obj{ lua.registry()[ENVY_RUN_DIR_RIDX] };
  if (!obj.valid() || obj.get_type() == sol::type::lua_nil) { return std::nullopt; }
  return std::filesystem::path{ obj.as<std::string>() };
}

phase_context_guard::phase_context_guard(engine *eng,
                                         recipe *r,
                                         std::optional<std::filesystem::path> run_dir)
    : lua_state_{ r ? r->lua->lua_state() : nullptr } {
  if (!lua_state_) { return; }

  sol::state_view lua{ lua_state_ };
  lua.registry()[ENVY_ENGINE_RIDX] = static_cast<void *>(eng);
  lua.registry()[ENVY_RECIPE_RIDX] = static_cast<void *>(r);
  if (run_dir) {
    lua.registry()[ENVY_RUN_DIR_RIDX] = run_dir->string();
  }
}

phase_context_guard::~phase_context_guard() {
  if (!lua_state_) { return; }

  sol::state_view lua{ lua_state_ };
  lua.registry()[ENVY_ENGINE_RIDX] = sol::lua_nil;
  lua.registry()[ENVY_RECIPE_RIDX] = sol::lua_nil;
  lua.registry()[ENVY_RUN_DIR_RIDX] = sol::lua_nil;
}

}  // namespace envy

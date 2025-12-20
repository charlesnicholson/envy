#include "lua_phase_context.h"

#include "recipe.h"

extern "C" {
#include "lua.h"
}

namespace envy {
namespace {

// Single registry key for phase context pointer (100 = OPTIONS, so use 103)
constexpr int ENVY_PHASE_CTX_RIDX = 103;

}  // namespace

phase_context const *lua_phase_context_get(sol::this_state ts) {
  sol::state_view lua{ ts.L };
  sol::object obj{ lua.registry()[ENVY_PHASE_CTX_RIDX] };
  if (!obj.valid() || obj.get_type() == sol::type::lua_nil) { return nullptr; }
  return static_cast<phase_context const *>(obj.as<void *>());
}

phase_context_guard::phase_context_guard(engine *eng,
                                         recipe *r,
                                         std::optional<std::filesystem::path> run_dir,
                                         cache::scoped_entry_lock const *lock)
    : lua_state_{ r ? r->lua->lua_state() : nullptr },
      ctx_{ eng, r, std::move(run_dir), lock } {
  if (!lua_state_) { return; }

  sol::state_view lua{ lua_state_ };
  lua.registry()[ENVY_PHASE_CTX_RIDX] = static_cast<void *>(&ctx_);
}

phase_context_guard::~phase_context_guard() {
  if (!lua_state_) { return; }

  sol::state_view lua{ lua_state_ };
  lua.registry()[ENVY_PHASE_CTX_RIDX] = sol::lua_nil;
}

}  // namespace envy

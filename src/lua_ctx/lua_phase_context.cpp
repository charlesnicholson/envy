#include "lua_phase_context.h"

#include "lua_envy.h"
#include "pkg.h"

namespace envy {

phase_context const *lua_phase_context_get(sol::this_state ts) {
  sol::state_view lua{ ts.L };
  sol::object obj{ lua.registry()[ENVY_PHASE_CTX_RIDX] };
  if (!obj.valid() || obj.get_type() == sol::type::lua_nil) { return nullptr; }
  return static_cast<phase_context const *>(obj.as<void *>());
}

phase_context_guard::phase_context_guard(engine *eng,
                                         pkg *p,
                                         std::optional<std::filesystem::path> run_dir,
                                         cache::scoped_entry_lock const *lock)
    : lua_state_{ p ? p->lua->lua_state() : nullptr },
      ctx_{ eng, p, std::move(run_dir), lock } {
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

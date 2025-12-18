#include "lua_phase_context.h"

namespace envy {
namespace {

thread_local engine *g_current_engine{ nullptr };
thread_local recipe *g_current_recipe{ nullptr };

}  // namespace

engine *lua_phase_context_get_engine() { return g_current_engine; }
recipe *lua_phase_context_get_recipe() { return g_current_recipe; }

phase_context_guard::phase_context_guard(engine *eng, recipe *r) {
  g_current_engine = eng;
  g_current_recipe = r;
}

phase_context_guard::~phase_context_guard() {
  g_current_engine = nullptr;
  g_current_recipe = nullptr;
}

}  // namespace envy

#include "lua_phase_context.h"

namespace envy {
namespace {

thread_local engine *g_current_engine{ nullptr };
thread_local recipe *g_current_recipe{ nullptr };
thread_local std::filesystem::path const *g_tmp_dir{ nullptr };
thread_local std::filesystem::path const *g_fetch_dir{ nullptr };

}  // namespace

engine *lua_phase_context_get_engine() { return g_current_engine; }
recipe *lua_phase_context_get_recipe() { return g_current_recipe; }
std::filesystem::path const *lua_phase_context_get_tmp_dir() { return g_tmp_dir; }
std::filesystem::path const *lua_phase_context_get_fetch_dir() { return g_fetch_dir; }

phase_context_guard::phase_context_guard(engine *eng, recipe *r) {
  g_current_engine = eng;
  g_current_recipe = r;
  g_tmp_dir = nullptr;
  g_fetch_dir = nullptr;
}

phase_context_guard::~phase_context_guard() {
  g_current_engine = nullptr;
  g_current_recipe = nullptr;
  g_tmp_dir = nullptr;
  g_fetch_dir = nullptr;
}

void phase_context_guard::set_tmp_dir(std::filesystem::path const *dir) {
  g_tmp_dir = dir;
}
void phase_context_guard::set_fetch_dir(std::filesystem::path const *dir) {
  g_fetch_dir = dir;
}

}  // namespace envy

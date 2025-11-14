#include "phase_check.h"

#include "blake3_util.h"
#include "tui.h"
#include "util.h"

#include <stdexcept>
#include <utility>

namespace envy {

void run_check_phase(recipe *r, graph_state &state) {
  std::string const key{ r->spec.format_key() };
  tui::trace("phase check START [%s]", key.c_str());
  trace_on_exit trace_end{ "phase check END [" + key + "]" };

  lua_State *lua = r->lua_state.get();
  if (!lua) { throw std::runtime_error("No lua_state for recipe: " + r->spec.identity); }

  lua_getglobal(lua, "check");
  bool const has_check{ lua_isfunction(lua, -1) };

  if (has_check) {
    lua_newtable(lua);

    if (lua_pcall(lua, 1, 1, 0) != LUA_OK) {
      char const *err{ lua_tostring(lua, -1) };
      lua_pop(lua, 1);
      throw std::runtime_error("check() failed for " + r->spec.identity + ": " +
                               (err ? err : "unknown error"));
    }

    bool const installed{ static_cast<bool>(lua_toboolean(lua, -1)) };
    lua_pop(lua, 1);

    if (installed) {
      tui::trace("phase check: user check returned true, asset already installed");
      return;
    }
  } else {
    lua_pop(lua, 1);
  }

  std::string const key_for_hash{ r->spec.format_key() };

  auto const digest{ blake3_hash(key_for_hash.data(), key_for_hash.size()) };
  r->canonical_identity_hash = util_bytes_to_hex(digest.data(), 32);  // Full hash: 64 hex chars
  std::string const hash_prefix{ util_bytes_to_hex(digest.data(), 8) };  // For cache path: 16 hex chars

  std::string const platform{ lua_global_to_string(lua, "ENVY_PLATFORM") };
  std::string const arch{ lua_global_to_string(lua, "ENVY_ARCH") };

  auto cache_result{
    state.cache_.ensure_asset(r->spec.identity, platform, arch, hash_prefix)
  };

  if (cache_result.lock) {  // Cache miss - acquire lock, subsequent phases will do work
    r->lock = std::move(cache_result.lock);
    tui::trace("phase check: [%s] CACHE MISS - pipeline will execute", key.c_str());
  } else {  // Cache hit - store asset_path, no lock means subsequent phases skip
    r->asset_path = cache_result.asset_path;
    tui::trace("phase check: [%s] CACHE HIT at %s - phases will skip",
               key.c_str(),
               cache_result.asset_path.string().c_str());
  }
}

}  // namespace envy

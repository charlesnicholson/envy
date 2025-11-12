#include "phase_check.h"

#include "blake3_util.h"
#include "tui.h"
#include "util.h"

#include <stdexcept>
#include <utility>

namespace envy {

void run_check_phase(std::string const &key, graph_state &state) {
  tui::trace("phase check START %s", key.c_str());
  trace_on_exit trace_end{ "phase check END " + key };

  lua_State *lua{ [&] {
    typename decltype(state.recipes)::const_accessor acc;
    if (!state.recipes.find(acc, key)) {
      throw std::runtime_error("Recipe not found for " + key);
    }
    return acc->second.lua_state.get();
  }() };

  lua_getglobal(lua, "check");
  bool const has_check{ lua_isfunction(lua, -1) };

  if (has_check) {
    lua_newtable(lua);

    if (lua_pcall(lua, 1, 1, 0) != LUA_OK) {
      char const *err{ lua_tostring(lua, -1) };
      lua_pop(lua, 1);
      throw std::runtime_error("check() failed for " + key + ": " +
                               (err ? err : "unknown error"));
    }

    bool const installed{ static_cast<bool>(lua_toboolean(lua, -1)) };
    lua_pop(lua, 1);

    if (installed) {
      typename decltype(state.recipes)::accessor acc;
      if (state.recipes.find(acc, key)) {
        acc->second.completion_node->try_put(tbb::flow::continue_msg{});
      }
      tui::trace("phase check: user check returned true, triggered completion");
      return;
    }
  } else {
    lua_pop(lua, 1);
  }

  auto const digest{ blake3_hash(key.data(), key.size()) };

  std::string const hash_prefix{ util_bytes_to_hex(digest.data(), 8) };

  std::string const platform{ lua_global_to_string(lua, "ENVY_PLATFORM") };
  std::string const arch{ lua_global_to_string(lua, "ENVY_ARCH") };

  std::string identity{ key };
  if (auto const brace_pos{ key.find('{') }; brace_pos != std::string::npos) {
    identity = key.substr(0, brace_pos);
  }

  auto cache_result{ state.cache_.ensure_asset(identity, platform, arch, hash_prefix) };

  typename decltype(state.recipes)::accessor acc;
  if (state.recipes.find(acc, key)) {
    if (cache_result.lock) {
      acc->second.lock = std::move(cache_result.lock);
      tui::trace("phase check: cache miss for %s, triggering fetch", key.c_str());
      tbb::flow::make_edge(*acc->second.deploy_node, *acc->second.completion_node);
      acc->second.fetch_node->try_put(tbb::flow::continue_msg{});
    } else {
      acc->second.asset_path = cache_result.asset_path;
      tui::trace("phase check: cache hit for %s, triggering completion", key.c_str());
      acc->second.completion_node->try_put(tbb::flow::continue_msg{});
    }
  }
}

}  // namespace envy

#pragma once

extern "C" {
#include "lua.h"
}

#include <filesystem>
#include <memory>

namespace envy {

using lua_state_ptr = std::unique_ptr<lua_State, void (*)(lua_State *)>;

// Standard Lua libraries loaded, print() overridden to use tui::info()
// envy.debug/info/warn/error/stdout for TUI output
lua_state_ptr lua_make();

bool lua_run_file(lua_state_ptr const &state, std::filesystem::path const &path);

bool lua_run_string(lua_state_ptr const &state, char const *script);

}  // namespace envy

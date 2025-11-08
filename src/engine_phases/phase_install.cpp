#include "phase_install.h"

#include "tui.h"

#include <filesystem>
#include <stdexcept>

namespace envy {

void run_install_phase(std::string const &key, graph_state &state) {
  tui::trace("phase install START %s", key.c_str());
  trace_on_exit trace_end{ "phase install END " + key };

  lua_State *lua{ [&] {
    typename decltype(state.recipes)::const_accessor acc;
    if (!state.recipes.find(acc, key)) {
      throw std::runtime_error("Recipe not found for " + key);
    }
    return acc->second.lua_state.get();
  }() };

  lua_getglobal(lua, "install");
  bool const has_install{ lua_isfunction(lua, -1) };

  if (has_install) {
    lua_newtable(lua);
    if (lua_pcall(lua, 1, 0, 0) != LUA_OK) {
      char const *err{ lua_tostring(lua, -1) };
      lua_pop(lua, 1);
      throw std::runtime_error("install() failed for " + key + ": " +
                               (err ? err : "unknown error"));
    }
  } else {
    lua_pop(lua, 1);
  }

  typename decltype(state.recipes)::accessor acc;
  if (state.recipes.find(acc, key)) {
    if (acc->second.lock) {
      auto const install_dir{ acc->second.lock->install_dir() };
      std::filesystem::create_directories(install_dir);

      // If no custom install function, copy staged/built files to install_dir
      if (!has_install) {
        auto const stage_dir{ acc->second.lock->stage_dir() };
        if (std::filesystem::exists(stage_dir) && std::filesystem::is_directory(stage_dir)) {
          tui::trace("phase install: copying staged files to install_dir");
          for (auto const &entry : std::filesystem::recursive_directory_iterator(stage_dir)) {
            if (entry.is_regular_file() || entry.is_symlink()) {
              auto const rel_path{ std::filesystem::relative(entry.path(), stage_dir) };
              auto const dest_path{ install_dir / rel_path };
              std::filesystem::create_directories(dest_path.parent_path());
              std::filesystem::copy(entry.path(), dest_path,
                                    std::filesystem::copy_options::overwrite_existing);
            }
          }
        }
      }

      std::filesystem::path const entry_path{ install_dir.parent_path() };
      std::filesystem::path const final_asset_path{ entry_path / "asset" };

      acc->second.lock->mark_install_complete();
      acc->second.asset_path = final_asset_path;
      acc->second.lock.reset();
    }
  }
}

}  // namespace envy

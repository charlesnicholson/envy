#include "lua_ctx_bindings.h"

#include "tui.h"

#include <filesystem>
#include <functional>
#include <string>

namespace envy {

std::function<void(std::string const &)> make_ctx_ls(lua_ctx_common *ctx) {
  return [ctx](std::string const &path_str) {
    std::filesystem::path const path{ path_str };

    tui::info("ctx.ls: %s", path.string().c_str());

    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
      tui::info("  (directory does not exist or is inaccessible)");
      return;
    }

    if (!std::filesystem::is_directory(path, ec)) {
      tui::info("  (not a directory)");
      return;
    }

    for (auto const &entry : std::filesystem::directory_iterator(path, ec)) {
      std::string const type{ entry.is_directory() ? "d" : "f" };
      tui::info("  [%s] %s", type.c_str(), entry.path().filename().string().c_str());
    }

    if (ec) { tui::info("  (error reading directory: %s)", ec.message().c_str()); }
  };
}

}  // namespace envy

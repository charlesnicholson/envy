#include "cmd_lua.h"

#include "lua_envy.h"
#include "sol_util.h"

#include "CLI11.hpp"
#include "sol/sol.hpp"

#include <memory>

namespace envy {

void cmd_lua::register_cli(CLI::App &app, std::function<void(cfg)> on_selected) {
  auto *sub{ app.add_subcommand("lua", "Execute Lua script") };
  auto cfg_ptr{ std::make_shared<cfg>() };
  sub->add_option("script", cfg_ptr->script_path, "Lua script file to execute")
      ->required()
      ->check(CLI::ExistingFile);
  sub->callback(
      [cfg_ptr, on_selected = std::move(on_selected)] { on_selected(*cfg_ptr); });
}

cmd_lua::cmd_lua(cmd_lua::cfg cfg,
                 std::optional<std::filesystem::path> const & /*cli_cache_root*/)
    : cfg_{ std::move(cfg) } {}

void cmd_lua::execute() {
  auto lua{ sol_util_make_lua_state() };
  lua_envy_install(*lua);

  sol::protected_function_result result =
      lua->safe_script_file(cfg_.script_path.string(), sol::script_pass_on_error);
  if (!result.valid()) {
    sol::error err = result;
    throw std::runtime_error(err.what());
  }
}

}  // namespace envy

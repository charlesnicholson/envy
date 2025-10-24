#pragma once

#include "cmd_extract.h"
#include "cmd_lua.h"
#include "cmd_playground.h"
#include "cmd_version.h"
#ifdef ENVY_FUNCTIONAL_TESTER
#include "cmd_cache_functional_test.h"
#endif
#include "tui.h"

#include <optional>
#include <string>
#include <variant>

namespace envy {

struct cli_args {
  using cmd_cfg_t = std::variant<cmd_extract::cfg,
                                  cmd_lua::cfg,
                                  cmd_playground::cfg,
                                  cmd_version::cfg
#ifdef ENVY_FUNCTIONAL_TESTER
                                  ,
                                  cmd_cache_ensure_asset::cfg,
                                  cmd_cache_ensure_recipe::cfg
#endif
                                  >;

  std::optional<cmd_cfg_t> cmd_cfg;
  std::optional<tui::level> verbosity;
  std::string cli_output;

  cli_args() : verbosity{ tui::level::TUI_DEBUG } {}
};

cli_args cli_parse(int argc, char **argv);

}  // namespace envy

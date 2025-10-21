#pragma once

#include "cmd_lua.h"
#include "cmd_playground.h"
#include "cmd_version.h"
#include "tui.h"

#include <optional>
#include <string>
#include <variant>

namespace envy {

struct cli_args {
  using cmd_cfg_t = std::variant<cmd_lua::cfg, cmd_playground::cfg, cmd_version::cfg>;

  std::optional<cmd_cfg_t> cmd_cfg;
  std::optional<tui::level> verbosity;
  std::string cli_output;
};

cli_args cli_parse(int argc, char **argv);

}  // namespace envy

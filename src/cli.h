#pragma once

#include "commands/cmd_asset.h"
#include "commands/cmd_extract.h"
#include "commands/cmd_fetch.h"
#include "commands/cmd_hash.h"
#include "commands/cmd_lua.h"
#include "commands/cmd_sync.h"
#include "commands/cmd_version.h"
#ifdef ENVY_FUNCTIONAL_TESTER
#include "commands/cmd_cache_functional_test.h"
#include "commands/cmd_engine_functional_test.h"
#endif
#include "tui.h"

#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace envy {

struct cli_args {
  using cmd_cfg_t = std::variant<cmd_asset::cfg,
                                 cmd_extract::cfg,
                                 cmd_fetch::cfg,
                                 cmd_hash::cfg,
                                 cmd_lua::cfg,
                                 cmd_sync::cfg,
                                 cmd_version::cfg
#ifdef ENVY_FUNCTIONAL_TESTER
                                 ,
                                 cmd_cache_ensure_asset::cfg,
                                 cmd_cache_ensure_recipe::cfg,
                                 cmd_engine_functional_test::cfg
#endif
                                 >;

  std::optional<cmd_cfg_t> cmd_cfg;
  std::optional<tui::level> verbosity;
  bool decorated_logging{ false };
  std::vector<tui::trace_output_spec> trace_outputs;
  std::string cli_output;
};

cli_args cli_parse(int argc, char **argv);

}  // namespace envy

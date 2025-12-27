#pragma once

#include "cmds/cmd_asset.h"
#include "cmds/cmd_extract.h"
#include "cmds/cmd_fetch.h"
#include "cmds/cmd_hash.h"
#include "cmds/cmd_init.h"
#include "cmds/cmd_lua.h"
#include "cmds/cmd_product.h"
#include "cmds/cmd_sync.h"
#include "cmds/cmd_version.h"
#ifdef ENVY_FUNCTIONAL_TESTER
#include "cmds/cmd_cache_functional_test.h"
#include "cmds/cmd_engine_functional_test.h"
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
                                 cmd_init::cfg,
                                 cmd_lua::cfg,
                                 cmd_product::cfg,
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
  std::optional<std::filesystem::path> cache_root;  // Global cache root override
  std::optional<tui::level> verbosity;
  bool decorated_logging{ false };
  std::vector<tui::trace_output_spec> trace_outputs;
  std::string cli_output;
};

cli_args cli_parse(int argc, char **argv);

}  // namespace envy

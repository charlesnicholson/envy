#include "cmd_lua.h"
#include "lua_util.h"

namespace envy {

cmd_lua::cmd_lua(cmd_lua::cfg cfg) : cfg_{ std::move(cfg) } {}

void cmd_lua::schedule(tbb::flow::graph &g) {
  node_.emplace(g, [this](tbb::flow::continue_msg const &) {
    auto L{ lua_make() };
    if (!L) {
      succeeded_ = false;
      return;
    }
    lua_add_tui(L);
    succeeded_ = lua_run_file(L, cfg_.script_path);
  });

  node_->try_put(tbb::flow::continue_msg{});
}

}  // namespace envy

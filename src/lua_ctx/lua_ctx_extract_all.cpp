#include "lua_ctx_bindings.h"

#include "extract.h"

#include <functional>

namespace envy {

std::function<void(sol::optional<sol::table>)> make_ctx_extract_all(lua_ctx_common *ctx) {
  return [ctx](sol::optional<sol::table> opts_table) {
    int strip_components{ 0 };

    if (opts_table) {
      sol::optional<int> strip = (*opts_table)["strip"];
      if (strip) {
        strip_components = *strip;
        if (strip_components < 0) {
          throw std::runtime_error("ctx.extract_all: strip must be non-negative");
        }
      }
    }

    extract_all_archives(ctx->fetch_dir, ctx->run_dir, strip_components);
  };
}

}  // namespace envy

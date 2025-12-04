#include "lua_ctx_bindings.h"

#include "extract.h"
#include "recipe.h"
#include "trace.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace envy {

std::function<int(std::string const &, sol::optional<sol::table>)> make_ctx_extract(
    lua_ctx_common *ctx) {
  return [ctx](std::string const &filename, sol::optional<sol::table> opts_table) -> int {
    int strip_components{ 0 };

    if (opts_table) {
      sol::optional<int> strip = (*opts_table)["strip"];
      if (strip) {
        strip_components = *strip;
        if (strip_components < 0) {
          throw std::runtime_error("ctx.extract: strip must be non-negative");
        }
      }
    }

    std::filesystem::path const archive_path{ ctx->fetch_dir / filename };

    if (!std::filesystem::exists(archive_path)) {
      throw std::runtime_error("ctx.extract: file not found: " + filename);
    }

    ENVY_TRACE_LUA_CTX_EXTRACT_START(ctx->recipe_->spec->identity,
                                     archive_path.string(),
                                     ctx->run_dir.string());

    auto const start_time{ std::chrono::steady_clock::now() };

    std::uint64_t const files{
      extract(archive_path, ctx->run_dir, { .strip_components = strip_components })
    };

    auto const duration_ms{ std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - start_time)
                                .count() };

    ENVY_TRACE_LUA_CTX_EXTRACT_COMPLETE(ctx->recipe_->spec->identity,
                                        static_cast<std::int64_t>(files),
                                        static_cast<std::int64_t>(duration_ms));

    return static_cast<int>(files);
  };
}

}  // namespace envy

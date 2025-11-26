#include "lua_ctx_bindings.h"

#include <filesystem>
#include <functional>
#include <string>

namespace envy {

std::function<void(std::string const &, std::string const &)> make_ctx_move(
    lua_ctx_common *ctx) {
  return [ctx](std::string const &src_str, std::string const &dst_str) {
    std::filesystem::path src{ src_str };
    std::filesystem::path dst{ dst_str };

    if (src.is_relative()) { src = ctx->run_dir / src; }
    if (dst.is_relative()) { dst = ctx->run_dir / dst; }

    if (!std::filesystem::exists(src)) {
      throw std::runtime_error("ctx.move: source not found: " + src_str);
    }

    if (std::filesystem::is_regular_file(src) && std::filesystem::is_directory(dst)) {
      dst = dst / src.filename();
    }

    if (dst.has_parent_path()) { std::filesystem::create_directories(dst.parent_path()); }

    if (std::filesystem::exists(dst)) {
      throw std::runtime_error("ctx.move: destination already exists: " + dst_str +
                               " (remove it explicitly first if you want to replace it)");
    }

    std::filesystem::rename(src, dst);
  };
}

}  // namespace envy

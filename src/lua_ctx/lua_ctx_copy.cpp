#include "lua_ctx_bindings.h"

#include <filesystem>
#include <functional>
#include <string>

namespace envy {

std::function<void(std::string const &, std::string const &)> make_ctx_copy(
    lua_ctx_common *ctx) {
  return [ctx](std::string const &src_str, std::string const &dst_str) {
    std::filesystem::path src{ src_str };
    std::filesystem::path dst{ dst_str };

    if (src.is_relative()) { src = ctx->run_dir / src; }
    if (dst.is_relative()) { dst = ctx->run_dir / dst; }

    if (!std::filesystem::exists(src)) {
      throw std::runtime_error("ctx.copy: source not found: " + src_str);
    }

    if (std::filesystem::is_regular_file(src) && std::filesystem::is_directory(dst)) {
      dst = dst / src.filename();
    }

    if (std::filesystem::is_directory(src)) {
      std::filesystem::copy(src,
                            dst,
                            std::filesystem::copy_options::recursive |
                                std::filesystem::copy_options::overwrite_existing);
    } else {
      if (dst.has_parent_path()) {
        std::filesystem::create_directories(dst.parent_path());
      }
      std::filesystem::copy_file(src,
                                 dst,
                                 std::filesystem::copy_options::overwrite_existing);
    }
  };
}

}  // namespace envy

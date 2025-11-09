#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace envy {

using shell_env_t = std::unordered_map<std::string, std::string>;
using shell_output_cb_t = std::function<void(std::string_view)>;

struct shell_invocation {
  std::optional<std::filesystem::path> cwd;
  shell_env_t env;
  shell_output_cb_t on_output_line;
};

shell_env_t shell_getenv();
int shell_run(std::string_view script, shell_invocation const &invocation);

}  // namespace envy

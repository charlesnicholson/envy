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

struct shell_result {
  int exit_code;
  bool signaled;
  int signal;  // Only valid if signaled == true
};

struct shell_invocation {
  std::function<void(std::string_view)> on_output_line;
  std::optional<std::filesystem::path> cwd;
  shell_env_t env;
  bool disable_strict{false};  // Disable default "set -euo pipefail"
};

shell_env_t shell_getenv();
shell_result shell_run(std::string_view script, shell_invocation const &invocation);

}  // namespace envy

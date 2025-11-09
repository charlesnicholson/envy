#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace envy {

using shell_env_t = std::unordered_map<std::string, std::string>;

struct shell_result {
  int exit_code;
  std::optional<int> signal;
};

enum class shell_choice { bash, sh, cmd, powershell };

struct shell_run_cfg {
  std::function<void(std::string_view)> on_output_line;
  std::optional<std::filesystem::path> cwd;
  shell_env_t env;
#if defined(_WIN32)
  shell_choice shell{ shell_choice::powershell };
#else
  shell_choice shell{ shell_choice::bash };
#endif
};

shell_choice shell_parse_choice(std::optional<std::string_view> value);

shell_env_t shell_getenv();
shell_result shell_run(std::string_view script, shell_run_cfg const &cfg);

}  // namespace envy

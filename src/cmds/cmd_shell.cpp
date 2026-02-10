#include "cmd_shell.h"

#include "cache.h"
#include "cmd_init.h"
#include "tui.h"

#include "CLI11.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace envy {

namespace fs = std::filesystem;

namespace {

struct shell_info {
  char const *name;
  char const *ext;
  char const *profile_hint;
  char const *source_fmt;  // printf format: %s = hook file path
};

// clang-format off
constexpr shell_info kShells[] = {
  {"bash",       "bash", "~/.bashrc",    "source \"%s\""},
  {"zsh",        "zsh",  "~/.zshrc",     "source \"%s\""},
  {"fish",       "fish", "~/.config/fish/config.fish", "source \"%s\""},
  {"powershell", "ps1",  "$PROFILE",     ". \"%s\""},
};
// clang-format on

shell_info const *find_shell(std::string const &name) {
  for (auto const &s : kShells) {
    if (s.name == name) { return &s; }
  }
  return nullptr;
}

bool is_custom_cache(std::optional<fs::path> const &cli_cache_root) {
  return cli_cache_root.has_value() || std::getenv("ENVY_CACHE_ROOT") != nullptr;
}

}  // namespace

void cmd_shell::register_cli(CLI::App &app,
                              std::function<void(cfg)> on_selected) {
  auto *sub{ app.add_subcommand(
      "shell", "Print shell hook source line for your profile") };
  auto cfg_ptr{ std::make_shared<cfg>() };
  sub->add_option("shell", cfg_ptr->shell, "Shell name (bash, zsh, fish, powershell)")
      ->required()
      ->check(CLI::IsMember({ "bash", "zsh", "fish", "powershell" }));
  sub->callback(
      [cfg_ptr, on_selected = std::move(on_selected)] { on_selected(*cfg_ptr); });
}

cmd_shell::cmd_shell(cmd_shell::cfg cfg,
                      std::optional<fs::path> const &cli_cache_root)
    : cfg_{ std::move(cfg) }, cli_cache_root_{ cli_cache_root } {}

void cmd_shell::execute() {
  auto const *si{ find_shell(cfg_.shell) };
  if (!si) {
    throw std::runtime_error(
        "shell: unsupported shell '" + cfg_.shell +
        "'. Use: bash, zsh, fish, powershell");
  }

  // Trigger self-deploy (which writes hook files)
  auto c{ cache::ensure(cli_cache_root_, std::nullopt) };

  fs::path const hook_path{ c->root() / "shell" / ("hook." + std::string{ si->ext }) };
  if (!fs::exists(hook_path)) {
    throw std::runtime_error("shell: hook file not found at " +
                             hook_path.string() +
                             ". Run any envy command to trigger self-deploy.");
  }

  std::string const portable{ make_portable_path(hook_path) };

  // Convert VS Code-style env placeholders to shell-native syntax for display
  std::string display_path{ portable };
  if (cfg_.shell != "powershell") {
    // bash/zsh/fish use $HOME; make_portable_path() returns ${env:HOME} on
    // Unix and ${env:USERPROFILE} on Windows — map both to $HOME.
    constexpr std::string_view kEnvHome{ "${env:HOME}" };
    constexpr std::string_view kEnvUserProfile{ "${env:USERPROFILE}" };

    auto pos{ display_path.find(kEnvHome) };
    if (pos != std::string::npos) {
      display_path.replace(pos, kEnvHome.size(), "$HOME");
    }
    pos = display_path.find(kEnvUserProfile);
    if (pos != std::string::npos) {
      display_path.replace(pos, kEnvUserProfile.size(), "$HOME");
    }
  }

  char buf[1024];
  std::snprintf(buf, sizeof(buf), si->source_fmt, display_path.c_str());

  tui::info("Add this line to %s:", si->profile_hint);
  tui::info("");
  tui::info("  %s", buf);
  tui::info("");

  if (is_custom_cache(cli_cache_root_)) {
    tui::warn("Hook files are stored in cache at %s", c->root().string().c_str());
    tui::warn("Moving or deleting this cache will break shell integration.");
  }

  tui::info("Then restart your shell or run the command directly.");
}

}  // namespace envy

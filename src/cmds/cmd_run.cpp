#include "cmd_run.h"

#include "manifest.h"
#include "platform.h"
#include "util.h"

#include "CLI11.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace envy {

namespace fs = std::filesystem;

void cmd_run::register_cli(CLI::App &app, std::function<void(cfg)> on_selected) {
  auto *sub{ app.add_subcommand("run", "Run a command with envy bin dir on PATH") };
  sub->prefix_command();
  auto cfg_ptr{ std::make_shared<cfg>() };
  sub->callback([sub, cfg_ptr, on_selected = std::move(on_selected)] {
    cfg_ptr->command = sub->remaining();
    on_selected(*cfg_ptr);
  });
}

cmd_run::cmd_run(cmd_run::cfg cfg, std::optional<fs::path> const & /*cli_cache_root*/)
    : cfg_{ std::move(cfg) } {}

void cmd_run::execute() {
  if (cfg_.command.empty()) { throw std::runtime_error("run: no command specified"); }

  auto const manifest_path{ manifest::find_manifest_path(std::nullopt) };
  auto const content{ util_load_file(manifest_path) };
  std::string_view const sv{ reinterpret_cast<char const *>(content.data()),
                             content.size() };
  auto const meta{ parse_envy_meta(sv) };

  if (!meta.bin) {
    throw std::runtime_error("run: manifest has no @envy bin directive: " +
                             manifest_path.string());
  }

  auto const bin_dir_raw{ manifest_path.parent_path() / *meta.bin };
  if (!fs::exists(bin_dir_raw) || !fs::is_directory(bin_dir_raw)) {
    throw std::runtime_error("run: bin directory does not exist: " + bin_dir_raw.string());
  }
  auto const bin_dir{ fs::canonical(bin_dir_raw) };

  auto const manifest_dir{ manifest_path.parent_path().string() };

  constexpr char kPathSep{
#ifdef _WIN32
    ';'
#else
    ':'
#endif
  };

  char const *existing_path{ std::getenv("PATH") };
  std::string new_path{ bin_dir.string() };
  if (existing_path && *existing_path) {
    new_path += kPathSep;
    new_path += existing_path;
  }

  platform::set_env_var("PATH", new_path.c_str());
  platform::set_env_var("ENVY_PROJECT_ROOT", manifest_dir.c_str());

  std::vector<char *> argv;
  argv.reserve(cfg_.command.size() + 1);
  for (auto &arg : cfg_.command) { argv.push_back(arg.data()); }
  argv.push_back(nullptr);

#ifdef _WIN32
  // Windows has no true execvp; spawn the child, wait, propagate its exit code.
  intptr_t const rc{ _spawnvp(_P_WAIT, argv[0], argv.data()) };
  if (rc == -1) {
    throw std::runtime_error(std::string{ "run: spawn failed: " } + std::strerror(errno));
  }
  throw subprocess_exit{ static_cast<int>(rc) };
#else
  execvp(argv[0], argv.data());
  throw std::runtime_error(std::string{ "run: exec failed: " } + std::strerror(errno));
#endif
}

}  // namespace envy

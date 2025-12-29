#include "cmd_hash.h"

#include "sha256.h"
#include "tui.h"
#include "util.h"

#include "CLI11.hpp"

#include <stdexcept>

namespace envy {

void cmd_hash::register_cli(CLI::App &app, std::function<void(cfg)> on_selected) {
  auto *sub{ app.add_subcommand("hash", "Compute SHA256 hash of a file") };
  auto *cfg_ptr{ new cfg{} };
  sub->add_option("file", cfg_ptr->file_path, "File to hash")
      ->required()
      ->check(CLI::ExistingFile);
  sub->callback(
      [cfg_ptr, on_selected = std::move(on_selected)] { on_selected(*cfg_ptr); });
}

cmd_hash::cmd_hash(cmd_hash::cfg cfg,
                   std::optional<std::filesystem::path> const & /*cli_cache_root*/)
    : cfg_{ std::move(cfg) } {}

void cmd_hash::execute() {
  if (cfg_.file_path.empty()) { throw std::runtime_error("hash: file path is required"); }

  if (!std::filesystem::exists(cfg_.file_path)) {
    throw std::runtime_error("hash: file does not exist: " + cfg_.file_path.string());
  }

  if (std::filesystem::is_directory(cfg_.file_path)) {
    throw std::runtime_error("hash: path is a directory: " + cfg_.file_path.string());
  }

  auto const hash{ sha256(cfg_.file_path) };
  auto const hex{ util_bytes_to_hex(hash.data(), hash.size()) };

  tui::print_stdout("%s\n", hex.c_str());
}

}  // namespace envy

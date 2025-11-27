#include "cmd_hash.h"

#include "sha256.h"
#include "tui.h"
#include "util.h"

#include <stdexcept>

namespace envy {

cmd_hash::cmd_hash(cmd_hash::cfg cfg) : cfg_{ std::move(cfg) } {}

bool cmd_hash::execute() {
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

  return true;
}

}  // namespace envy

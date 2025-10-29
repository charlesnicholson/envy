#include "cmd_extract.h"
#include "tui.h"

#include "extract.h"

#include <filesystem>
#include <string>

namespace envy {
cmd_extract::cmd_extract(cmd_extract::cfg cfg) : cfg_{ std::move(cfg) } {}

bool cmd_extract::execute() {
  std::filesystem::path destination{ cfg_.destination };
  if (destination.empty()) { destination = std::filesystem::current_path(); }

  std::error_code ec;
  if (!std::filesystem::exists(cfg_.archive_path, ec)) {
    tui::error("Failed to extract: archive not found: %s",
               cfg_.archive_path.string().c_str());
    return false;
  }

  if (!std::filesystem::is_regular_file(cfg_.archive_path, ec)) {
    tui::error("Failed to extract: not a regular file: %s",
               cfg_.archive_path.string().c_str());
    return false;
  }

  if (!std::filesystem::exists(destination, ec)) {
    std::filesystem::create_directories(destination, ec);
    if (ec) {
      tui::error("Failed to create destination directory: %s", ec.message().c_str());
      return false;
    }
  }

  if (!std::filesystem::is_directory(destination, ec)) {
    tui::error("Destination is not a directory: %s", destination.string().c_str());
    return false;
  }

  try {
    tui::info("Extracting %s to %s",
              cfg_.archive_path.filename().string().c_str(),
              destination.string().c_str());

    auto const file_count{ extract(cfg_.archive_path, destination) };
    tui::info("Extracted %llu files", static_cast<unsigned long long>(file_count));
    return true;
  } catch (std::exception const &ex) {
    tui::error("Extraction failed: %s", ex.what());
    return false;
  }
}

}  // namespace envy

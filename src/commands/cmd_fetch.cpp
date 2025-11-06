#include "cmd_fetch.h"

#include "fetch.h"
#include "tui.h"

#include <filesystem>
#include <string>

namespace envy {

cmd_fetch::cmd_fetch(cmd_fetch::cfg cfg) : cfg_{ std::move(cfg) } {}

bool cmd_fetch::execute() {
  if (cfg_.source.empty()) {
    tui::error("fetch: source URI is empty");
    return false;
  }

  if (cfg_.destination.empty()) {
    tui::error("fetch: destination path is empty");
    return false;
  }

  fetch_request request{ .source = cfg_.source,
                         .destination = cfg_.destination,
                         .file_root = cfg_.manifest_root,
                         .progress = {} };

  try {
    auto const results{ fetch({ request }) };
    if (results.empty()) {
      tui::error("fetch failed: no result returned");
      return false;
    }

    if (std::holds_alternative<std::string>(results[0])) {
      tui::error("fetch failed: %s", std::get<std::string>(results[0]).c_str());
      return false;
    }

    auto const &result{ std::get<fetch_result>(results[0]) };
    tui::info("Fetched %s -> %s",
              result.resolved_source.string().c_str(),
              result.resolved_destination.string().c_str());
    return true;
  } catch (std::exception const &ex) {
    tui::error("fetch failed: %s", ex.what());
    return false;
  }
}

}  // namespace envy

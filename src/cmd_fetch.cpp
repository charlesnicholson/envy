#include "cmd_fetch.h"

#include "fetch.h"
#include "tui.h"

#include <filesystem>
#include <string>

namespace envy {

cmd_fetch::cmd_fetch(cmd_fetch::cfg cfg) : cfg_{ std::move(cfg) } {}

void cmd_fetch::schedule(tbb::flow::graph &g) {
  node_.emplace(g, [this](tbb::flow::continue_msg const &) {
    if (cfg_.source.empty()) {
      tui::error("fetch: source URI is empty");
      succeeded_ = false;
      return;
    }

    if (cfg_.destination.empty()) {
      tui::error("fetch: destination path is empty");
      succeeded_ = false;
      return;
    }

    fetch_request request{ .source = cfg_.source,
                           .destination = cfg_.destination,
                           .file_root = cfg_.manifest_root,
                           .progress = {} };

    try {
      auto const result{ fetch(request) };
      tui::info("Fetched %s -> %s",
                result.resolved_source.string().c_str(),
                result.resolved_destination.string().c_str());
      succeeded_ = true;
    } catch (std::exception const &ex) {
      tui::error("fetch failed: %s", ex.what());
      succeeded_ = false;
    }
  });

  node_->try_put(tbb::flow::continue_msg{});
}

}  // namespace envy

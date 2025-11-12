#include "cmd_fetch.h"

#include "fetch.h"
#include "tui.h"
#include "uri.h"

#include <filesystem>
#include <stdexcept>
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

  // Determine the request type based on URL scheme
  auto const info{ uri_classify(cfg_.source) };
  fetch_request req;
  switch (info.scheme) {
    case uri_scheme::HTTP:
      req = fetch_request_http{ .source = cfg_.source, .destination = cfg_.destination };
      break;
    case uri_scheme::HTTPS:
      req = fetch_request_https{ .source = cfg_.source, .destination = cfg_.destination };
      break;
    case uri_scheme::FTP:
      req = fetch_request_ftp{ .source = cfg_.source, .destination = cfg_.destination };
      break;
    case uri_scheme::FTPS:
      req = fetch_request_ftps{ .source = cfg_.source, .destination = cfg_.destination };
      break;
    case uri_scheme::S3:
      req = fetch_request_s3{ .source = cfg_.source, .destination = cfg_.destination };
      break;
    case uri_scheme::LOCAL_FILE_ABSOLUTE:
    case uri_scheme::LOCAL_FILE_RELATIVE:
      req = fetch_request_file{ .source = cfg_.source,
                                .destination = cfg_.destination,
                                .file_root =
                                    cfg_.manifest_root.value_or(std::filesystem::path{}) };
      break;
    case uri_scheme::GIT:
      if (!cfg_.ref.has_value() || cfg_.ref->empty()) {
        tui::error("fetch: git sources require --ref <branch|tag|sha>");
        return false;
      }
      req = fetch_request_git{ .source = info.canonical,
                               .destination = cfg_.destination,
                               .ref = *cfg_.ref };
      break;
    default: tui::error("fetch: unsupported URL scheme"); return false;
  }

  try {
    auto const results{ fetch({ req }) };
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

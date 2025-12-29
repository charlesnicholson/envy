#include "cmd_fetch.h"

#include "fetch.h"
#include "tui.h"
#include "uri.h"

#include "CLI11.hpp"

#include <filesystem>
#include <stdexcept>
#include <string>

namespace envy {

void cmd_fetch::register_cli(CLI::App &app, std::function<void(cfg)> on_selected) {
  auto *sub{ app.add_subcommand("fetch", "Download resource to local file") };
  auto *cfg_ptr{ new cfg{} };
  sub->add_option("source", cfg_ptr->source, "Source URI (http/https/git/etc.)")
      ->required();
  sub->add_option("destination", cfg_ptr->destination, "Destination file path")
      ->required();
  sub->add_option("--manifest-root",
                  cfg_ptr->manifest_root,
                  "Manifest root for resolving relative file URIs");
  sub->add_option("--ref", cfg_ptr->ref, "Git ref (branch/tag/SHA) for git sources");
  sub->callback(
      [cfg_ptr, on_selected = std::move(on_selected)] { on_selected(*cfg_ptr); });
}

cmd_fetch::cmd_fetch(cmd_fetch::cfg cfg,
                     std::optional<std::filesystem::path> const & /*cli_cache_root*/)
    : cfg_{ std::move(cfg) } {}

void cmd_fetch::execute() {
  if (cfg_.source.empty()) { throw std::runtime_error("fetch: source URI is empty"); }
  if (cfg_.destination.empty()) {
    throw std::runtime_error("fetch: destination path is empty");
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
        throw std::runtime_error("fetch: git sources require --ref <branch|tag|sha>");
      }
      req = fetch_request_git{ .source = info.canonical,
                               .destination = cfg_.destination,
                               .ref = *cfg_.ref };
      break;
    default: throw std::runtime_error("fetch: unsupported URL scheme");
  }

  auto const results{ fetch({ req }) };
  if (results.empty()) { throw std::runtime_error("fetch: no result returned"); }

  if (std::holds_alternative<std::string>(results[0])) {
    throw std::runtime_error("fetch: " + std::get<std::string>(results[0]));
  }

  auto const &result{ std::get<fetch_result>(results[0]) };
  tui::debug("Fetched %s -> %s",
             result.resolved_source.string().c_str(),
             result.resolved_destination.string().c_str());
}

}  // namespace envy

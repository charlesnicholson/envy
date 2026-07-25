#include "cmd_mirror_envy.h"

#include "aws_util.h"
#include "envy_release.h"
#include "fetch.h"
#include "platform.h"
#include "tui.h"
#include "uri.h"
#include "util.h"

#include "CLI11.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace envy {

namespace fs = std::filesystem;

namespace {

std::string_view strip_trailing_slashes(std::string_view s) {
  while (s.ends_with('/')) { s.remove_suffix(1); }
  return s;
}

// Owns a scratch tree for the duration of the run. util.h's scoped_path_cleanup removes a
// single entry, which is not enough here -- the staging directory has archives in it.
class scoped_temp_dir : unmovable {
 public:
  explicit scoped_temp_dir(std::filesystem::path path) : path_{ std::move(path) } {}

  ~scoped_temp_dir() {
    if (auto const ec{ platform::remove_all_with_retry(path_) }) {
      tui::warn("mirror-envy: failed to remove staging directory %s: %s",
                path_.string().c_str(),
                ec.message().c_str());
    }
  }

  std::filesystem::path const &path() const { return path_; }

 private:
  std::filesystem::path path_;
};

// A destination is a local directory or an s3:// URI. Everything else -- git, ssh, http --
// is rejected here rather than surfacing later as a confusing fetch error.
void validate_dest(std::string_view dest) {
  if (dest.empty()) { throw std::runtime_error("mirror-envy: destination is empty"); }

  // "s3:/bucket" has no "://", so uri_classify would call it a relative path and we would
  // silently stage into a local directory named "s3:".
  bool const looks_s3{ dest.size() >= 3 && (dest[0] == 's' || dest[0] == 'S') &&
                       dest[1] == '3' && dest[2] == ':' };
  if (looks_s3 && !(dest.size() >= 5 && dest.substr(3, 2) == "//")) {
    throw std::runtime_error("mirror-envy: malformed S3 destination '" +
                             std::string{ dest } + "' (expected s3://bucket/prefix)");
  }
}

}  // namespace

std::string mirror_envy_s3_root(mirror_envy_plan const &plan) {
  std::ostringstream ss;
  ss << "s3://" << plan.bucket;
  if (!plan.prefix.empty()) { ss << '/' << plan.prefix; }
  return ss.str();
}

std::string mirror_envy_s3_uri(mirror_envy_plan const &plan, std::string_view relpath) {
  std::ostringstream ss;
  ss << mirror_envy_s3_root(plan) << '/' << relpath;
  return ss.str();
}

mirror_envy_plan mirror_envy_make_plan(std::string_view version,
                                       std::string_view dest,
                                       std::string_view from_mirror) {
  if (!envy_release_version_is_valid(version)) {
    throw std::runtime_error("mirror-envy: invalid version string: " +
                             std::string{ version });
  }

  auto const from{ strip_trailing_slashes(from_mirror) };
  if (from.empty()) { throw std::runtime_error("mirror-envy: source mirror is empty"); }

  validate_dest(dest);

  mirror_envy_plan plan{};

  switch (auto const info{ uri_classify(dest) }; info.scheme) {
    case uri_scheme::S3: {
      auto const parts{ aws_s3_parse_uri(info.canonical, "mirror-envy") };
      plan.dest_is_s3 = true;
      plan.bucket = parts.bucket;
      plan.prefix = parts.key;  // already stripped of trailing slashes
      break;
    }
    case uri_scheme::LOCAL_FILE_ABSOLUTE:
    case uri_scheme::LOCAL_FILE_RELATIVE: plan.local_dir = info.canonical; break;
    default:
      throw std::runtime_error(
          "mirror-envy: destination must be a local directory or an s3:// URI (got '" +
          std::string{ dest } + "')");
  }

  plan.items.reserve(kEnvyReleaseTargets.size());
  for (auto const &target : kEnvyReleaseTargets) {
    std::ostringstream rel;
    rel << 'v' << version << '/' << envy_release_archive_name(target.os, target.arch);
    plan.items.push_back(mirror_envy_item{
        .source_url = envy_release_url(from, version, target.os, target.arch),
        .relpath = rel.str() });
  }

  return plan;
}

void cmd_mirror_envy::register_cli(CLI::App &app, std::function<void(cfg)> on_selected) {
  auto *sub{ app.add_subcommand("mirror-envy",
                                "Mirror an envy release for all platforms to a directory "
                                "or S3 prefix") };
  auto cfg_ptr{ std::make_shared<cfg>() };
  cfg_ptr->from = std::string{ kEnvyReleaseDownloadUrl };

  sub->add_option("version", cfg_ptr->version, "Envy version to mirror (e.g. 1.2.3)")
      ->required();
  sub->add_option("destination",
                  cfg_ptr->dest,
                  "Local directory or s3://bucket/prefix to mirror into")
      ->required();
  sub->add_option("--from",
                  cfg_ptr->from,
                  "Source mirror to read the release from (default: envy's GitHub "
                  "releases)");
  sub->callback(
      [cfg_ptr, on_selected = std::move(on_selected)] { on_selected(*cfg_ptr); });
}

cmd_mirror_envy::cmd_mirror_envy(cmd_mirror_envy::cfg cfg,
                                 std::optional<fs::path> const & /*cli_cache_root*/)
    : cfg_{ std::move(cfg) } {}

void cmd_mirror_envy::execute() {
  auto const plan{ mirror_envy_make_plan(cfg_.version, cfg_.dest, cfg_.from) };

  // For an S3 destination the archives still have to land on disk first, because the AWS
  // upload API takes a file. That scratch tree must be uniquely created rather than named
  // predictably -- otherwise another user in a shared temp dir could pre-create the path
  // and see, or substitute, what gets uploaded -- and it must not outlive the run: six
  // release archives per invocation adds up. Use a local destination instead to keep the
  // staged bytes.
  std::optional<scoped_temp_dir> scratch;
  if (plan.dest_is_s3) {
    scratch.emplace(platform::create_unique_temp_dir("envy-mirror"));
  }
  auto const &staging{ scratch ? scratch->path() : plan.local_dir };

  std::error_code ec;
  fs::create_directories(staging / ("v" + cfg_.version), ec);
  if (ec) {
    throw std::runtime_error("mirror-envy: failed to create " + staging.string() + ": " +
                             ec.message());
  }
  if (plan.dest_is_s3) {
    // Debug, not info: the tree is transient scratch that is removed before we return.
    tui::debug("mirror-envy: staging in %s", staging.string().c_str());
  }

  std::vector<fetch_request> requests;
  requests.reserve(plan.items.size());
  for (auto const &item : plan.items) {
    requests.push_back(fetch_request_from_url(item.source_url, staging / item.relpath));
  }

  auto const results{ fetch(requests) };
  if (results.size() != plan.items.size()) {
    throw std::runtime_error("mirror-envy: fetch returned " +
                             std::to_string(results.size()) + " results for " +
                             std::to_string(plan.items.size()) + " requests");
  }

  size_t failed{ 0 };
  for (size_t i{ 0 }; i < results.size(); ++i) {
    if (auto const *error{ std::get_if<std::string>(&results[i]) }) {
      tui::error("mirror-envy: %s: %s", plan.items[i].source_url.c_str(), error->c_str());
      ++failed;
    } else {
      tui::debug("mirror-envy: fetched %s", plan.items[i].relpath.c_str());
    }
  }
  if (failed > 0) {
    throw std::runtime_error("mirror-envy: " + std::to_string(failed) + " of " +
                             std::to_string(plan.items.size()) +
                             " archives failed to download");
  }

  // Mirror-root "latest" so a bootstrap script can resolve the newest version from the
  // mirror rather than probing github.com. Format matches the cache's own latest file:
  // the bare version, no trailing newline.
  util_write_file(staging / kMirrorLatestFile, cfg_.version);

  if (!plan.dest_is_s3) {
    tui::info("mirror-envy: staged envy %s (%zu archives) in %s",
              cfg_.version.c_str(),
              plan.items.size(),
              staging.string().c_str());
    tui::info("mirror-envy: upload with: aws s3 cp --recursive %s s3://<bucket>/<prefix>",
              staging.string().c_str());
    return;
  }

  // Thread per object, matching fetch()'s shape. Errors are collected so one bad key does
  // not hide the others.
  std::vector<std::string> relpaths;
  relpaths.reserve(plan.items.size() + 1);
  for (auto const &item : plan.items) { relpaths.push_back(item.relpath); }
  relpaths.emplace_back(kMirrorLatestFile);

  std::vector<std::string> errors(relpaths.size());
  {
    std::vector<std::thread> workers;
    workers.reserve(relpaths.size());
    for (size_t i{ 0 }; i < relpaths.size(); ++i) {
      workers.emplace_back([&, i] {
        auto const uri{ mirror_envy_s3_uri(plan, relpaths[i]) };
        try {
          aws_s3_upload(s3_upload_request{ .source = staging / relpaths[i], .uri = uri });
          tui::debug("mirror-envy: uploaded %s", uri.c_str());
        } catch (std::exception const &e) {
          errors[i] = uri + ": " + e.what();
        } catch (...) { errors[i] = uri + ": unknown error during upload"; }
      });
    }
    for (auto &t : workers) { t.join(); }
  }

  size_t upload_failures{ 0 };
  for (auto const &error : errors) {
    if (error.empty()) { continue; }
    tui::error("mirror-envy: %s", error.c_str());
    ++upload_failures;
  }
  if (upload_failures > 0) {
    throw std::runtime_error("mirror-envy: " + std::to_string(upload_failures) + " of " +
                             std::to_string(relpaths.size()) + " uploads failed");
  }

  auto const root{ mirror_envy_s3_root(plan) };
  tui::info("mirror-envy: mirrored envy %s (%zu objects) to %s",
            cfg_.version.c_str(),
            relpaths.size(),
            root.c_str());
  tui::info("mirror-envy: point envy.lua at it with:");
  tui::info("  -- @envy version \"%s\"", cfg_.version.c_str());
  tui::info("  -- @envy mirror \"%s\"", root.c_str());
}

}  // namespace envy

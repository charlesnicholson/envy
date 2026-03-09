#include "phase_import.h"

#include "cache.h"
#include "engine.h"
#include "extract.h"
#include "fetch.h"
#include "package_depot.h"
#include "pkg.h"
#include "pkg_cfg.h"
#include "sha256.h"
#include "trace.h"
#include "tui.h"
#include "tui_actions.h"
#include "util.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>

namespace envy {

namespace {

bool directory_has_entries(std::filesystem::path const &dir) {
  std::error_code ec;
  std::filesystem::directory_iterator it{ dir, ec };
  return !ec && it != std::filesystem::directory_iterator{};
}

}  // namespace

void run_import_phase(pkg *p, engine &eng) {
  phase_trace_scope const phase_scope{ p->cfg->identity,
                                       pkg_phase::pkg_import,
                                       std::chrono::steady_clock::now() };

  if (!p->lock) { return; }  // Cache hit — no work needed

  if (p->type == pkg_type::USER_MANAGED) { return; }  // Never imported from package depot

  auto const *depot{ eng.depot_index() };
  if (!depot || depot->empty()) { return; }  // No depot configured

  sol::state_view lua{ *p->lua };
  std::string const platform{ lua["envy"]["PLATFORM"].get<std::string>() };
  std::string const arch{ lua["envy"]["ARCH"].get<std::string>() };
  std::string const hash_prefix{ p->canonical_identity_hash.substr(0, 16) };

  auto const location{ depot->find(p->cfg->identity, platform, arch, hash_prefix) };
  if (!location) { return; }  // Depot miss — fall through to fetch

  namespace fs = std::filesystem;

  tui::debug("phase import: [%s] depot hit: %s",
             p->cfg->identity.c_str(),
             location->url.c_str());

  std::string const label{ "[" + p->cfg->identity + "]" };

  try {
    fs::path archive_path;

    // Local file: use directly; remote URL: download to temp dir
    if (fs::path const local_path{ location->url }; fs::exists(local_path)) {
      archive_path = fs::absolute(local_path);
    } else {
      fs::path const tmp_dir{ p->lock->tmp_dir() };
      fs::path const depot_fetch_dir{ tmp_dir / "depot-fetch" };
      fs::create_directory(depot_fetch_dir);
      archive_path = depot_fetch_dir / "depot-archive.tar.zst";

      std::vector<fetch_request> requests;
      requests.push_back(fetch_request_from_url(location->url, archive_path));

      tui_actions::fetch_progress_tracker tracker{ p->tui_section,
                                                   p->cfg->identity,
                                                   location->url };
      std::visit([&](auto &r) { r.progress = tracker; }, requests[0]);

      auto const results{ fetch(requests) };
      if (results.empty() || !std::holds_alternative<fetch_result>(results[0])) {
        auto const *error{ results.empty() ? nullptr
                                           : std::get_if<std::string>(&results[0]) };
        tui::warn("depot: failed to download archive %s: %s",
                  location->url.c_str(),
                  error ? error->c_str() : "unknown error");
        return;  // Fall through to fetch phase
      }
    }

    // SHA256 verification when present (text manifests always supply it;
    // only build_from_directory without checksums omits it).
    if (location->sha256) {
      tui::section_set_content(
          p->tui_section,
          tui::section_frame{ .label = label,
                              .content = tui::spinner_data{
                                  .text = "verifying SHA256...",
                                  .start_time = std::chrono::steady_clock::now() } });

      auto const actual{ sha256(archive_path) };
      auto const actual_hex{ util_bytes_to_hex(actual.data(), actual.size()) };
      if (actual_hex != *location->sha256) {
        tui::warn("depot: SHA256 mismatch for %s (expected %s, got %s)",
                  location->url.c_str(),
                  location->sha256->c_str(),
                  actual_hex.c_str());
        return;  // Fall through to fetch/build
      }
    }

    // entry_path is lock->install_dir().parent_path()
    fs::path const entry_path{ p->lock->install_dir().parent_path() };

    // Pre-scan archive for totals, then extract with progress bar.
    tui::section_set_content(
        p->tui_section,
        tui::section_frame{ .label = label,
                            .content = tui::spinner_data{
                                .text = "analyzing archive...",
                                .start_time = std::chrono::steady_clock::now() } });

    auto const totals{ compute_archive_totals(archive_path) };

    std::uint64_t files_done{ 0 };
    std::uint64_t bytes_done{ 0 };
    fs::path last_file;

    extract_options opts{ .progress = [&](extract_progress const &ep) -> bool {
      bytes_done = ep.bytes_processed;
      if (ep.is_regular_file && ep.current_entry != last_file) {
        ++files_done;
        last_file = ep.current_entry;
      }

      double percent{ 0.0 };
      if (totals.files > 0) {
        percent =
            std::min(100.0, (files_done / static_cast<double>(totals.files)) * 100.0);
      } else if (totals.bytes > 0) {
        percent =
            std::min(100.0, (bytes_done / static_cast<double>(totals.bytes)) * 100.0);
      }

      std::string status;
      status.reserve(64);
      status += std::to_string(files_done);
      if (totals.files > 0) {
        status += "/";
        status += std::to_string(totals.files);
      }
      status += " files";
      if (totals.bytes > 0) {
        status += " ";
        status += util_format_bytes(bytes_done);
        status += "/";
        status += util_format_bytes(totals.bytes);
      }

      tui::section_set_content(
          p->tui_section,
          tui::section_frame{
              .label = label,
              .content = tui::progress_data{ .percent = percent, .status = status } });
      return true;
    } };

    extract(archive_path, entry_path, opts);

    bool const has_install{ directory_has_entries(p->lock->install_dir()) };
    bool const has_fetch{ directory_has_entries(p->lock->fetch_dir()) };

    if (has_install) {
      p->lock->mark_install_complete();
      p->pkg_path = p->lock->install_dir();
      p->lock.reset();
      tui::section_set_content(
          p->tui_section,
          tui::section_frame{ .label = label,
                              .content = tui::static_text_data{ .text = "imported" } });
      tui::debug("phase import: [%s] depot import complete at %s",
                 p->cfg->identity.c_str(),
                 p->pkg_path.string().c_str());
    } else if (has_fetch) {
      p->lock->mark_fetch_complete();
      tui::section_set_content(p->tui_section,
                               tui::section_frame{ .label = label,
                                                   .content = tui::static_text_data{
                                                       .text = "imported (fetch)" } });
      tui::debug("phase import: [%s] depot fetch-only import, build phases will continue",
                 p->cfg->identity.c_str());
    } else {
      tui::warn("depot: archive %s did not populate pkg/ or fetch/ directories",
                location->url.c_str());
    }
  } catch (std::exception const &e) {
    tui::warn("depot: failed to import archive %s: %s", location->url.c_str(), e.what());
  }
}

}  // namespace envy

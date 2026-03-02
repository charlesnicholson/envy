#include "phase_import.h"

#include "cache.h"
#include "engine.h"
#include "extract.h"
#include "fetch.h"
#include "package_depot.h"
#include "pkg.h"
#include "pkg_cfg.h"
#include "trace.h"
#include "tui.h"

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
             location->c_str());

  try {
    fs::path const tmp_dir{ p->lock->tmp_dir() };
    fs::path const depot_fetch_dir{ tmp_dir / "depot-fetch" };
    fs::create_directory(depot_fetch_dir);
    fs::path const archive_dest{ depot_fetch_dir / "depot-archive.tar.zst" };

    // Local file: symlink into depot-fetch dir; remote URL: download
    if (fs::path const local_path{ *location }; fs::exists(local_path)) {
      fs::create_symlink(local_path, archive_dest);
    } else {
      std::vector<fetch_request> requests;
      requests.push_back(fetch_request_from_url(*location, archive_dest));

      auto const results{ fetch(requests) };
      if (results.empty() || !std::holds_alternative<fetch_result>(results[0])) {
        auto const *error{ results.empty() ? nullptr
                                           : std::get_if<std::string>(&results[0]) };
        tui::warn("depot: failed to download archive %s: %s",
                  location->c_str(),
                  error ? error->c_str() : "unknown error");
        return;  // Fall through to fetch phase
      }
    }

    // entry_path is lock->install_dir().parent_path()
    fs::path const entry_path{ p->lock->install_dir().parent_path() };

    extract_all_archives(depot_fetch_dir, entry_path, 0, p->cfg->identity, p->tui_section);

    bool const has_install{ directory_has_entries(p->lock->install_dir()) };
    bool const has_fetch{ directory_has_entries(p->lock->fetch_dir()) };

    if (has_install) {
      p->lock->mark_install_complete();
      p->pkg_path = p->lock->install_dir();
      p->lock.reset();
      tui::debug("phase import: [%s] depot import complete at %s",
                 p->cfg->identity.c_str(),
                 p->pkg_path.string().c_str());
    } else if (has_fetch) {
      p->lock->mark_fetch_complete();
      tui::debug("phase import: [%s] depot fetch-only import, build phases will continue",
                 p->cfg->identity.c_str());
    } else {
      tui::warn("depot: archive %s did not populate pkg/ or fetch/ directories",
                location->c_str());
    }
  } catch (std::exception const &e) {
    tui::warn("depot: failed to import archive %s: %s", location->c_str(), e.what());
  }
}

}  // namespace envy

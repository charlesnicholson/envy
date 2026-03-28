#include "phase_export.h"

#include "engine.h"
#include "extract.h"
#include "pkg.h"
#include "sha256.h"
#include "trace.h"
#include "tui.h"
#include "util.h"

#include <chrono>
#include <filesystem>
#include <sstream>
#include <utility>

namespace envy {

void run_export_phase(pkg *p, engine &eng) {
  phase_trace_scope const phase_scope{ p->cfg->identity,
                                       pkg_phase::pkg_export,
                                       std::chrono::steady_clock::now() };

  auto const *ecfg{ eng.export_config() };
  if (!ecfg) { return; }  // Not an export run; short-circuit.

  if (p->type != pkg_type::CACHE_MANAGED) {
    if (ecfg->explicitly_requested) {
      eng.record_export_result(
          p->key,
          "skipped non-cache-managed package " + std::string(p->key.identity()));
    }
    return;
  }

  if (!ecfg->export_targets.contains(p->key)) { return; }  // Not a target.

  bool const exportable{ [&] {
    sol::object obj{ sol::state_view{ *p->lua }["EXPORTABLE"] };
    return obj.valid() && obj.get_type() == sol::type::boolean && obj.as<bool>();
  }() };

  std::string const prefix{ exportable ? "pkg" : "fetch" };
  std::filesystem::path const entry_dir{ p->pkg_path.parent_path() };
  std::filesystem::path const source_dir{ entry_dir / prefix };

  {
    std::error_code ec;
    std::filesystem::directory_iterator it{ source_dir, ec };
    if (ec || it == std::filesystem::directory_iterator{}) {
      throw std::runtime_error(
          "export: " + prefix + "/ directory is empty or missing for " +
          std::string(p->key.identity()) + " (package may predate export support)");
    }
  }

  std::string const filename{ std::string(p->key.identity()) + "-" +
                              entry_dir.filename().string() + ".tar.zst" };
  std::filesystem::path const output_path{ ecfg->output_dir / filename };

  std::string const label{ "[" + std::string(p->key.identity()) + "]" };

  // Scan source to compute totals
  if (p->tui_section) {
    tui::section_set_content(
        p->tui_section,
        tui::section_frame{ .label = label,
                            .content = tui::spinner_data{
                                .text = "scanning...",
                                .start_time = std::chrono::steady_clock::now() } });
  }

  auto const [total_bytes, total_files]{ [&] {
    std::uint64_t bytes{ 0 }, files{ 0 };
    for (auto const &e : std::filesystem::recursive_directory_iterator(source_dir)) {
      if (e.is_regular_file()) {
        bytes += e.file_size();
        ++files;
      }
    }
    return std::pair{ bytes, files };
  }() };

  // Compress with progress
  archive_create_tar_zst(
      output_path,
      source_dir,
      prefix,
      [&](extract_progress const &ep) -> bool {
        if (!p->tui_section) { return true; }

        double percent{ 0.0 };
        if (total_bytes > 0) {
          percent =
              std::min(100.0,
                       (ep.bytes_processed / static_cast<double>(total_bytes)) * 100.0);
        } else if (total_files > 0) {
          percent =
              std::min(100.0,
                       (ep.files_processed / static_cast<double>(total_files)) * 100.0);
        }

        std::ostringstream status;
        status << ep.files_processed;
        if (total_files > 0) { status << "/" << total_files; }
        status << " files";
        if (total_bytes > 0) {
          status << " " << util_format_bytes(ep.bytes_processed) << "/"
                 << util_format_bytes(total_bytes);
        }

        tui::section_set_content(p->tui_section,
                                 tui::section_frame{ .label = label,
                                                     .content = tui::progress_data{
                                                         .percent = percent,
                                                         .status = status.str() } });
        return true;
      });

  // Hash the archive
  if (p->tui_section) {
    tui::section_set_content(
        p->tui_section,
        tui::section_frame{ .label = label,
                            .content = tui::spinner_data{
                                .text = "hashing...",
                                .start_time = std::chrono::steady_clock::now() } });
  }

  auto const hash{ sha256(output_path) };
  auto const hex{ util_bytes_to_hex(hash.data(), hash.size()) };

  std::string const path_part{ ecfg->depot_prefix ? (*ecfg->depot_prefix + filename)
                                                  : output_path.string() };

  eng.record_export_result(p->key, hex + "  " + path_part + "\n");
}

}  // namespace envy

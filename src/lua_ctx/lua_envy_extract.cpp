#include "lua_envy_extract.h"

#include "extract.h"
#include "lua_phase_context.h"
#include "tui_actions.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace envy {

void lua_envy_extract_install(sol::table &envy_table) {
  // envy.extract(archive_path, dest_dir, opts?) - Single archive extraction
  envy_table["extract"] = [](std::string const &archive_path_str,
                             std::string const &dest_dir_str,
                             sol::optional<sol::table> opts_table) -> int {
    int strip_components{ 0 };

    if (opts_table) {
      sol::optional<int> strip = (*opts_table)["strip"];
      if (strip) {
        strip_components = *strip;
        if (strip_components < 0) {
          throw std::runtime_error("envy.extract: strip must be non-negative");
        }
      }
    }

    std::filesystem::path const archive_path{ archive_path_str };
    std::filesystem::path const dest_dir{ dest_dir_str };

    if (!std::filesystem::exists(archive_path)) {
      throw std::runtime_error("envy.extract: file not found: " + archive_path.string());
    }

    // Set up progress tracking if in phase context
    recipe *r{ lua_phase_context_get_recipe() };
    std::optional<tui_actions::extract_progress_tracker> tracker;
    if (r && r->tui_section) {
      tracker.emplace(r->tui_section, r->spec->identity, archive_path.filename().string());
    }

    std::uint64_t const files{ extract(
        archive_path,
        dest_dir,
        { .strip_components = strip_components,
          .progress = tracker ? std::ref(*tracker) : extract_progress_cb_t{} }) };

    return static_cast<int>(files);
  };

  // envy.extract_all(src_dir, dest_dir, opts?) - Extract all archives in directory
  envy_table["extract_all"] = [](std::string const &src_dir_str,
                                 std::string const &dest_dir_str,
                                 sol::optional<sol::table> opts_table) {
    int strip_components{ 0 };

    if (opts_table) {
      sol::optional<int> strip = (*opts_table)["strip"];
      if (strip) {
        strip_components = *strip;
        if (strip_components < 0) {
          throw std::runtime_error("envy.extract_all: strip must be non-negative");
        }
      }
    }

    std::filesystem::path const src_dir{ src_dir_str };
    std::filesystem::path const dest_dir{ dest_dir_str };

    if (!std::filesystem::exists(src_dir)) {
      throw std::runtime_error("envy.extract_all: source directory not found: " +
                               src_dir.string());
    }

    extract_all_archives(src_dir, dest_dir, strip_components);
  };
}

}  // namespace envy

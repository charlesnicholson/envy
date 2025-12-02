#include "lua_ctx_bindings.h"

#include "sha256.h"
#include "tui.h"

#include <filesystem>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace envy {

namespace {

struct commit_entry {
  std::string filename;
  std::string sha256;  // empty = no verification
};

// Parse ctx.commit_fetch() arguments into commit entries
std::vector<commit_entry> parse_commit_fetch_args(sol::object const &arg) {
  std::vector<commit_entry> entries;

  if (arg.is<std::string>()) {
    // Single string: "filename"
    entries.push_back({ arg.as<std::string>(), "" });
  } else if (arg.is<sol::table>()) {
    sol::table tbl{ arg.as<sol::table>() };

    // Check if it's an array or a single table
    sol::object first_elem{ tbl[1] };

    if (first_elem.get_type() == sol::type::lua_nil) {
      // Single table: {filename="...", sha256="..."}
      sol::optional<std::string> filename = tbl["filename"];
      if (!filename) {
        throw std::runtime_error("ctx.commit_fetch: table missing 'filename' field");
      }
      sol::optional<std::string> sha256 = tbl["sha256"];
      entries.push_back({ *filename, sha256.value_or("") });
    } else if (first_elem.is<std::string>()) {
      // Array of strings: {"file1", "file2"}
      for (auto const &[key, value] : tbl) {
        if (!value.is<std::string>()) {
          throw std::runtime_error("ctx.commit_fetch: array elements must be strings");
        }
        entries.push_back({ value.as<std::string>(), "" });
      }
    } else if (first_elem.is<sol::table>()) {
      // Array of tables: {{filename="...", sha256="..."}, {...}}
      for (auto const &[key, value] : tbl) {
        if (!value.is<sol::table>()) {
          throw std::runtime_error("ctx.commit_fetch: array elements must be tables");
        }
        sol::table item_tbl{ value.as<sol::table>() };
        sol::optional<std::string> filename = item_tbl["filename"];
        if (!filename) {
          throw std::runtime_error(
              "ctx.commit_fetch: array element missing 'filename' field");
        }
        sol::optional<std::string> sha256 = item_tbl["sha256"];
        entries.push_back({ *filename, sha256.value_or("") });
      }
    } else {
      throw std::runtime_error("ctx.commit_fetch: invalid array element type");
    }
  } else {
    throw std::runtime_error("ctx.commit_fetch: argument must be string or table");
  }

  return entries;
}

// Verify SHA256 and move files from tmp_dir to fetch_dir
void commit_files(std::vector<commit_entry> const &entries,
                  std::filesystem::path const &tmp_dir,
                  std::filesystem::path const &fetch_dir) {
  std::vector<std::string> errors;

  for (auto const &entry : entries) {
    std::filesystem::path const src{ tmp_dir / entry.filename };
    std::filesystem::path const dest{ fetch_dir / entry.filename };

    // Check source exists
    if (!std::filesystem::exists(src)) {
      errors.push_back(entry.filename + ": file not found in tmp directory");
      continue;
    }

    // Verify SHA256 if provided
    if (!entry.sha256.empty()) {
      try {
        tui::debug("ctx.commit_fetch: verifying SHA256 for %s", entry.filename.c_str());
        sha256_verify(entry.sha256, sha256(src));
      } catch (std::exception const &e) {
        errors.push_back(entry.filename + ": " + e.what());
        continue;
      }
    }

    // Move file
    try {
      std::filesystem::rename(src, dest);
      tui::debug("ctx.commit_fetch: moved %s to fetch_dir", entry.filename.c_str());
    } catch (std::exception const &e) {
      errors.push_back(entry.filename + ": failed to move: " + e.what());
    }
  }

  if (!errors.empty()) {
    std::ostringstream oss;
    oss << "ctx.commit_fetch failed:\n";
    for (auto const &err : errors) { oss << "  " << err << "\n"; }
    throw std::runtime_error(oss.str());
  }
}

}  // namespace

std::function<void(sol::object)> make_ctx_commit_fetch(fetch_phase_ctx *ctx) {
  return [ctx](sol::object arg) {
    commit_files(parse_commit_fetch_args(arg), ctx->run_dir, ctx->fetch_dir);
  };
}

}  // namespace envy

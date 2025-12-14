#include "phase_stage.h"

#include "cache.h"
#include "engine.h"
#include "extract.h"
#include "lua_ctx/lua_ctx_bindings.h"
#include "lua_envy.h"
#include "lua_error_formatter.h"
#include "recipe.h"
#include "shell.h"
#include "sol_util.h"
#include "trace.h"
#include "tui.h"
#include "util.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace envy {
namespace {

// Count files in fetch_dir and check if any are archives
struct fetch_dir_info {
  std::uint64_t archive_count{ 0 };
  std::uint64_t archive_bytes{ 0 };
  std::uint64_t plain_count{ 0 };
  std::uint64_t plain_bytes{ 0 };

  bool empty() const { return archive_count == 0 && plain_count == 0; }
  std::uint64_t total_bytes() const { return archive_bytes + plain_bytes; }
};

fetch_dir_info analyze_fetch_dir(std::filesystem::path const &fetch_dir) {
  fetch_dir_info info;
  if (!std::filesystem::exists(fetch_dir)) { return info; }

  for (auto const &entry : std::filesystem::directory_iterator(fetch_dir)) {
    if (!entry.is_regular_file()) { continue; }
    std::string const filename{ entry.path().filename().string() };
    if (filename == "envy-complete") { continue; }

    std::error_code ec;
    auto const size{ std::filesystem::file_size(entry.path(), ec) };
    if (ec) {
      throw std::runtime_error("phase stage: failed to stat " + entry.path().string() +
                               ": " + ec.message());
    }

    if (extract_is_archive_extension(entry.path())) {
      ++info.archive_count;
      info.archive_bytes += size;
    } else {
      ++info.plain_count;
      info.plain_bytes += size;
    }
  }
  return info;
}

void render_stage_progress(tui::section_handle handle,
                           std::string const &label,
                           extract_progress const &prog) {
  std::ostringstream status;
  status << prog.files_processed;
  if (prog.total_files) { status << "/" << *prog.total_files; }
  status << " files";

  if (prog.total_bytes) {
    status << " " << util_format_bytes(prog.bytes_processed) << "/"
           << util_format_bytes(*prog.total_bytes);
  } else if (prog.bytes_processed > 0) {
    status << " " << util_format_bytes(prog.bytes_processed);
  }

  if (!prog.current_entry.empty()) {
    status << " " << prog.current_entry.filename().string();
  }

  if (prog.total_bytes) {
    double percent{ 0.0 };
    if (*prog.total_bytes > 0) {
      percent = std::min(
          100.0,
          (prog.bytes_processed / static_cast<double>(*prog.total_bytes)) * 100.0);
    }
    tui::section_set_content(
        handle,
        tui::section_frame{
            .label = label,
            .content = tui::progress_data{ .percent = percent, .status = status.str() } });
  } else {
    static auto const epoch{ std::chrono::steady_clock::time_point{} };
    tui::section_set_content(
        handle,
        tui::section_frame{
            .label = label,
            .content = tui::spinner_data{ .text = status.str(), .start_time = epoch } });
  }
}

void render_stage_complete(tui::section_handle handle,
                           std::string const &label,
                           extract_progress const &prog) {
  std::ostringstream status;
  status << prog.files_processed;
  if (prog.total_files) { status << "/" << *prog.total_files; }
  status << " files";
  if (prog.total_bytes) { status << " " << util_format_bytes(prog.bytes_processed); }

  tui::section_set_content(
      handle,
      tui::section_frame{ .label = label,
                          .content = tui::static_text_data{ .text = status.str() } });
}

struct stage_phase_ctx : lua_ctx_common {
  // run_dir inherited from base is dest_dir (stage_dir)
};

sol::table build_stage_phase_ctx_table(sol::state_view lua,
                                       std::string const &identity,
                                       stage_phase_ctx *ctx) {
  sol::table ctx_table{ lua.create_table() };
  ctx_table["identity"] = identity;
  ctx_table["fetch_dir"] = ctx->fetch_dir.string();
  ctx_table["stage_dir"] = ctx->run_dir.string();
  lua_ctx_add_common_bindings(ctx_table, ctx);
  return ctx_table;
}

std::filesystem::path determine_stage_destination(sol::state_view lua,
                                                  cache::scoped_entry_lock const *lock) {
  sol::object stage_obj{ lua["STAGE"] };
  sol::object build_obj{ lua["BUILD"] };
  sol::object install_obj{ lua["INSTALL"] };

  bool const has_custom_phases{ stage_obj.is<sol::protected_function>() ||
                                build_obj.is<sol::protected_function>() ||
                                install_obj.is<sol::protected_function>() };

  std::filesystem::path const dest_dir{ has_custom_phases ? lock->stage_dir()
                                                          : lock->install_dir() };

  tui::debug("phase stage: destination=%s (custom_phases=%s)",
             dest_dir.string().c_str(),
             has_custom_phases ? "true" : "false");

  return dest_dir;
}

struct stage_options {
  int strip_components{ 0 };
};

stage_options parse_stage_options(sol::table const &stage_tbl, std::string const &key) {
  stage_options opts;

  if (auto strip{ sol_util_get_optional<int>(stage_tbl, "strip", key) }) {
    if (*strip < 0) {
      throw std::runtime_error("stage.strip must be non-negative for " + key);
    }
    opts.strip_components = *strip;
  }

  return opts;
}

void run_extract_stage(fetch_dir_info const &info,
                       extract_totals const &totals,
                       std::filesystem::path const &fetch_dir,
                       std::filesystem::path const &dest_dir,
                       std::string const &identity,
                       std::string const &label,
                       tui::section_handle section,
                       int strip_components) {
  tui::debug("phase stage: extracting (strip=%d)", strip_components);

  // Build item list for per-file grouping
  std::vector<std::string> items;
  for (auto const &entry : std::filesystem::directory_iterator(fetch_dir)) {
    if (!entry.is_regular_file()) { continue; }
    if (entry.path().filename() == "envy-complete") { continue; }
    items.push_back(entry.path().filename().string());
  }
  bool const grouped{ items.size() > 1 };

  struct stage_state {
    std::mutex mutex;
    std::vector<tui::section_frame> children;
    std::optional<std::size_t> current_idx;
    bool grouped{ false };
    std::string label;
  } state;

  state.grouped = grouped;
  state.label = label;
  state.children.reserve(items.size());
  for (auto const &name : items) {
    state.children.push_back(
        tui::section_frame{ .label = name,
                            .content = tui::static_text_data{ .text = "pending" } });
  }

  std::optional<extract_progress> last_prog{ std::nullopt };

  auto set_stage_frames{ [&](extract_progress const &prog) {
    last_prog = prog;
    double percent{ 0.0 };
    if (prog.total_files && *prog.total_files > 0) {
      percent = (prog.files_processed / static_cast<double>(*prog.total_files)) * 100.0;
    } else if (prog.total_bytes && *prog.total_bytes > 0) {
      percent = (prog.bytes_processed / static_cast<double>(*prog.total_bytes)) * 100.0;
    }
    if (percent > 100.0) { percent = 100.0; }

    std::ostringstream status;
    status << "stage " << prog.files_processed;
    if (prog.total_files) { status << "/" << *prog.total_files; }
    status << " files";
    if (prog.total_bytes) {
      status << " " << util_format_bytes(prog.bytes_processed) << "/"
             << util_format_bytes(*prog.total_bytes);
    } else if (prog.bytes_processed > 0) {
      status << " " << util_format_bytes(prog.bytes_processed);
    }

    if (state.grouped) {
      tui::section_frame parent{ .label = state.label,
                                 .content = tui::progress_data{ .percent = percent,
                                                                .status = status.str() },
                                 .children = state.children,
                                 .phase_label = {} };
      tui::section_set_content(section, parent);
    } else {
      std::string item{ state.children.empty() ? "" : state.children.front().label };
      tui::section_set_content(
          section,
          tui::section_frame{
              .label = state.label,
              .content = tui::progress_data{
                  .percent = percent,
                  .status = item.empty() ? status.str() : (item + " " + status.str()) } });
    }
  } };

  extract_progress initial_progress{
    .bytes_processed = 0,
    .total_bytes = totals.bytes > 0 ? std::make_optional(totals.bytes) : std::nullopt,
    .files_processed = 0,
    .total_files = totals.files > 0 ? std::make_optional(totals.files) : std::nullopt,
    .current_entry = {},
    .is_regular_file = false
  };
  set_stage_frames(initial_progress);

  auto progress_cb{ extract_progress_cb_t{ [&](extract_progress const &prog) -> bool {
    std::lock_guard const lock{ state.mutex };
    set_stage_frames(prog);
    return true;
  } } };

  std::optional<std::size_t> last_idx;

  auto on_file{ [&](std::string const &name) {
    std::lock_guard const lock{ state.mutex };
    if (last_idx && *last_idx < state.children.size()) {
      state.children[*last_idx].content = tui::static_text_data{ .text = "done" };
    }

    if (auto it{ std::find_if(state.children.begin(),
                              state.children.end(),
                              [&](auto const &c) { return c.label == name; }) };
        it != state.children.end()) {
      auto idx{ static_cast<std::size_t>(std::distance(state.children.begin(), it)) };
      state.current_idx = idx;
      last_idx = idx;
      state.children[idx].content =
          tui::spinner_data{ .text = "staging",
                             .start_time = std::chrono::steady_clock::now() };
    }
    set_stage_frames(last_prog.value_or(initial_progress));
  } };

  extract_all_archives(
      fetch_dir,
      dest_dir,
      strip_components,
      progress_cb,
      identity,
      items.size() > 1 ? on_file : std::function<void(std::string const &)>{},
      totals);

  if (last_idx && *last_idx < state.children.size()) {
    std::lock_guard const lock{ state.mutex };
    state.children[*last_idx].content = tui::static_text_data{ .text = "done" };
    set_stage_frames(last_prog.value_or(initial_progress));
  }
}

void run_programmatic_stage(sol::protected_function stage_func,
                            std::filesystem::path const &fetch_dir,
                            std::filesystem::path const &dest_dir,
                            std::string const &identity,
                            engine &eng,
                            recipe *r) {
  tui::debug("phase stage: running imperative stage function");

  stage_phase_ctx ctx{};
  ctx.fetch_dir = fetch_dir;
  ctx.run_dir = dest_dir;
  ctx.engine_ = &eng;
  ctx.recipe_ = r;

  sol::state_view lua{ stage_func.lua_state() };
  sol::table ctx_table{ build_stage_phase_ctx_table(lua, identity, &ctx) };

  // Get options from registry and pass as 2nd arg
  sol::object opts{ lua.registry()[ENVY_OPTIONS_RIDX] };

  call_lua_function_with_enriched_errors(r, "STAGE", [&]() {
    return stage_func(ctx_table, opts);
  });
}

void run_shell_stage(std::string_view script,
                     std::filesystem::path const &dest_dir,
                     std::string const &identity,
                     resolved_shell shell) {
  tui::debug("phase stage: running shell script");

  shell_env_t env{ shell_getenv() };

  std::vector<std::string> output_lines;
  shell_run_cfg inv{ .on_output_line =
                         [&](std::string_view line) {
                           tui::info("%.*s", static_cast<int>(line.size()), line.data());
                           output_lines.emplace_back(line);
                         },
                     .cwd = dest_dir,
                     .env = std::move(env),
                     .shell = std::move(shell) };

  shell_result const result{ shell_run(script, inv) };

  if (result.exit_code != 0) {
    if (result.signal) {
      throw std::runtime_error("Stage shell script failed for " + identity +
                               " (terminated by signal " + std::to_string(*result.signal) +
                               ")");
    } else {
      throw std::runtime_error("Stage shell script failed for " + identity +
                               " (exit code " + std::to_string(result.exit_code) + ")");
    }
  }
}

}  // namespace

void run_stage_phase(recipe *r, engine &eng) {
  phase_trace_scope const phase_scope{ r->spec->identity,
                                       recipe_phase::asset_stage,
                                       std::chrono::steady_clock::now() };

  cache::scoped_entry_lock *lock{ r->lock.get() };
  if (!lock) {  // Cache hit - no work to do
    tui::debug("phase stage: no lock (cache hit), skipping");
    // Release section on cache hit (fetch will have already set final content)
    tui::section_release(r->tui_section);
    return;
  }

  std::string const &identity{ r->spec->identity };
  sol::state_view lua_view{ *r->lua };
  std::filesystem::path const dest_dir{ determine_stage_destination(lua_view, lock) };
  std::filesystem::path const fetch_dir{ lock->fetch_dir() };

  sol::object stage_obj{ lua_view["STAGE"] };

  // Analyze fetch_dir to determine display strategy
  fetch_dir_info const info{ analyze_fetch_dir(fetch_dir) };
  extract_totals const totals{ compute_extract_totals(fetch_dir) };

  // If there are no files to extract, skip stage entirely (don't overwrite fetch progress)
  if (info.empty()) {
    tui::debug("phase stage: no files in fetch_dir, skipping");
    return;
  }

  std::string const label{ "[" + identity + "]" };

  if (!stage_obj.valid()) {
    run_extract_stage(info,
                      totals,
                      fetch_dir,
                      dest_dir,
                      identity,
                      label,
                      r->tui_section,
                      0);
  } else if (stage_obj.is<std::string>()) {
    auto const script_str{ stage_obj.as<std::string>() };
    run_shell_stage(script_str,
                    dest_dir,
                    identity,
                    shell_resolve_default(r->default_shell_ptr));
  } else if (stage_obj.is<sol::protected_function>()) {
    run_programmatic_stage(stage_obj.as<sol::protected_function>(),
                           fetch_dir,
                           dest_dir,
                           identity,
                           eng,
                           r);
  } else if (stage_obj.is<sol::table>()) {
    stage_options const opts{ parse_stage_options(stage_obj.as<sol::table>(), identity) };
    run_extract_stage(info,
                      totals,
                      fetch_dir,
                      dest_dir,
                      identity,
                      label,
                      r->tui_section,
                      opts.strip_components);
  } else {
    throw std::runtime_error("STAGE field must be nil, string, table, or function for " +
                             identity);
  }
}

}  // namespace envy

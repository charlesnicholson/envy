#include "phase_build.h"

#include "cache.h"
#include "engine.h"
#include "lua_ctx/lua_ctx_bindings.h"
#include "lua_envy.h"
#include "lua_error_formatter.h"
#include "recipe.h"
#include "shell.h"
#include "trace.h"
#include "tui.h"
#include "util.h"

#include <chrono>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace envy {
namespace {

struct build_phase_ctx : lua_ctx_common {
  // run_dir inherited from base is stage_dir (build working directory)
  std::filesystem::path install_dir;
};

struct build_output_sink {
  tui::section_handle section;
  std::string label;
  std::chrono::steady_clock::time_point start_time;
  std::vector<std::string> lines;
  std::string header_text;
};

std::string flatten_and_simplify_script(std::string_view script,
                                        std::filesystem::path const &cache_root) {
  // First flatten the script with semicolon delimiters
  std::string const flattened{ util_flatten_script_with_semicolons(script) };

  // Then simplify cache paths
  return util_simplify_cache_paths(flattened, cache_root);
}

void append_build_output(build_output_sink &sink, std::string_view line) {
  if (!sink.section) { return; }

  sink.lines.emplace_back(line);
  tui::section_set_content(sink.section,
                           tui::section_frame{ .label = sink.label,
                                               .content = tui::text_stream_data{
                                                   .lines = sink.lines,
                                                   .line_limit = 3,
                                                   .start_time = sink.start_time,
                                                   .header_text = sink.header_text } });
}

sol::table build_build_phase_ctx_table(sol::state_view lua,
                                       std::string const &identity,
                                       build_phase_ctx *ctx) {
  sol::table ctx_table{ lua.create_table() };

  ctx_table["identity"] = identity;
  ctx_table["fetch_dir"] = ctx->fetch_dir.string();
  ctx_table["stage_dir"] = ctx->run_dir.string();
  ctx_table["install_dir"] = ctx->install_dir.string();

  lua_ctx_add_common_bindings(ctx_table, ctx);
  return ctx_table;
}

void run_programmatic_build(
    sol::protected_function build_func,
    std::filesystem::path const &fetch_dir,
    std::filesystem::path const &stage_dir,
    std::filesystem::path const &install_dir,
    std::string const &identity,
    engine &eng,
    recipe *r,
    std::function<void(std::string_view)> const &on_output_line,
    std::function<void(std::string_view)> const &on_command_start) {
  tui::debug("phase build: running programmatic build function");

  build_phase_ctx ctx{};
  ctx.fetch_dir = fetch_dir;
  ctx.run_dir = stage_dir;
  ctx.engine_ = &eng;
  ctx.recipe_ = r;
  ctx.install_dir = install_dir;
  ctx.on_output_line = on_output_line;
  ctx.on_command_start = on_command_start;

  sol::state_view lua{ build_func.lua_state() };
  sol::table ctx_table{ build_build_phase_ctx_table(lua, identity, &ctx) };
  sol::object opts{ lua.registry()[ENVY_OPTIONS_RIDX] };

  call_lua_function_with_enriched_errors(r, "BUILD", [&]() {
    return build_func(ctx_table, opts);
  });
}

void run_shell_build(std::string_view script,
                     std::filesystem::path const &stage_dir,
                     std::string const &identity,
                     resolved_shell shell,
                     std::function<void(std::string_view)> const &on_output_line) {
  tui::debug("phase build: running shell script");

  shell_env_t env{ shell_getenv() };

  std::string stdout_buffer;
  std::string stderr_buffer;
  shell_run_cfg const inv{
    .on_output_line =
        [&](std::string_view line) {
          if (on_output_line) {
            on_output_line(line);
          } else {
            tui::info("%.*s", static_cast<int>(line.size()), line.data());
          }
        },
    .on_stdout_line = [&](std::string_view line) { (stdout_buffer += line) += '\n'; },
    .on_stderr_line = [&](std::string_view line) { (stderr_buffer += line) += '\n'; },
    .cwd = stage_dir,
    .env = std::move(env),
    .shell = std::move(shell)
  };

  if (shell_result const result{ shell_run(script, inv) }; result.exit_code != 0) {
    if (!stdout_buffer.empty()) { tui::error("%s", stdout_buffer.c_str()); }
    if (!stderr_buffer.empty()) { tui::error("%s", stderr_buffer.c_str()); }

    if (result.signal) {
      throw std::runtime_error("Build shell script terminated by signal " +
                               std::to_string(*result.signal) + " for " + identity);
    } else {
      throw std::runtime_error("Build shell script failed for " + identity +
                               " (exit code " + std::to_string(result.exit_code) + ")");
    }
  }
}

}  // namespace

void run_build_phase(recipe *r, engine &eng) {
  phase_trace_scope const phase_scope{ r->spec->identity,
                                       recipe_phase::asset_build,
                                       std::chrono::steady_clock::now() };
  if (!r->lock) {
    tui::debug("phase build: no lock (cache hit), skipping");
    return;
  }

  sol::state_view lua_view{ *r->lua };
  sol::object build_obj{ lua_view["BUILD"] };

  if (!build_obj.valid()) {
    tui::debug("phase build: no build field, skipping");
  } else if (build_obj.is<std::string>()) {
    std::string const script{ build_obj.as<std::string>() };
    std::string const header{ flatten_and_simplify_script(script, eng.cache_root()) };

    build_output_sink sink{ .section = r->tui_section,
                            .label = "[" + r->spec->identity + "]",
                            .start_time = std::chrono::steady_clock::now(),
                            .lines = {},
                            .header_text = header };

    // Set initial content with spinner before running build
    tui::section_set_content(sink.section,
                             tui::section_frame{ .label = sink.label,
                                                 .content = tui::spinner_data{
                                                     .text = header,
                                                     .start_time = sink.start_time } });

    auto const on_output_line{ [&](std::string_view line) {
      append_build_output(sink, line);
    } };

    run_shell_build(script,
                    r->lock->stage_dir(),
                    r->spec->identity,
                    shell_resolve_default(r->default_shell_ptr),
                    on_output_line);
  } else if (build_obj.is<sol::protected_function>()) {
    build_output_sink sink{ .section = r->tui_section,
                            .label = "[" + r->spec->identity + "]",
                            .start_time = std::chrono::steady_clock::now(),
                            .lines = {},
                            .header_text = {} };

    auto const on_output_line{ [&](std::string_view line) {
      append_build_output(sink, line);
    } };

    auto const on_command_start{ [&](std::string_view cmd) {
      sink.header_text = flatten_and_simplify_script(cmd, eng.cache_root());
      // Update TUI immediately with spinner showing the actual command
      tui::section_set_content(sink.section,
                               tui::section_frame{ .label = sink.label,
                                                   .content = tui::spinner_data{
                                                       .text = sink.header_text,
                                                       .start_time = sink.start_time } });
    } };

    run_programmatic_build(build_obj.as<sol::protected_function>(),
                           r->lock->fetch_dir(),
                           r->lock->stage_dir(),
                           r->lock->install_dir(),
                           r->spec->identity,
                           eng,
                           r,
                           on_output_line,
                           on_command_start);
  } else {
    throw std::runtime_error("BUILD field must be nil, string, or function for " +
                             r->spec->identity);
  }
}

}  // namespace envy

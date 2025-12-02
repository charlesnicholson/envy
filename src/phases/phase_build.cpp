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

#include <chrono>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace envy {
namespace {

struct build_phase_ctx : lua_ctx_common {
  // run_dir inherited from base is stage_dir (build working directory)
  std::filesystem::path install_dir;
};

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

void run_programmatic_build(sol::protected_function build_func,
                            std::filesystem::path const &fetch_dir,
                            std::filesystem::path const &stage_dir,
                            std::filesystem::path const &install_dir,
                            std::string const &identity,
                            engine &eng,
                            recipe *r) {
  tui::debug("phase build: running programmatic build function");

  build_phase_ctx ctx{};
  ctx.fetch_dir = fetch_dir;
  ctx.run_dir = stage_dir;
  ctx.engine_ = &eng;
  ctx.recipe_ = r;
  ctx.install_dir = install_dir;

  sol::state_view lua{ build_func.lua_state() };
  sol::table ctx_table{ build_build_phase_ctx_table(lua, identity, &ctx) };
  sol::object opts{ lua.registry()[ENVY_OPTIONS_RIDX] };

  call_lua_function_with_enriched_errors(r, "build", [&]() {
    return build_func(ctx_table, opts);
  });
}

void run_shell_build(std::string_view script,
                     std::filesystem::path const &stage_dir,
                     std::string const &identity) {
  tui::debug("phase build: running shell script");

  shell_env_t env{ shell_getenv() };

  std::vector<std::string> output_lines;
  shell_run_cfg const inv{ .on_output_line =
                               [&](std::string_view line) {
                                 tui::info("%.*s",
                                           static_cast<int>(line.size()),
                                           line.data());
                                 output_lines.emplace_back(line);
                               },
                           .cwd = stage_dir,
                           .env = std::move(env),
                           .shell = shell_parse_choice(std::nullopt) };

  if (shell_result const result{ shell_run(script, inv) }; result.exit_code != 0) {
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
  sol::object build_obj{ lua_view["build"] };

  if (!build_obj.valid()) {
    tui::debug("phase build: no build field, skipping");
  } else if (build_obj.is<std::string>()) {
    run_shell_build(build_obj.as<std::string>(), r->lock->stage_dir(), r->spec->identity);
  } else if (build_obj.is<sol::protected_function>()) {
    run_programmatic_build(build_obj.as<sol::protected_function>(),
                           r->lock->fetch_dir(),
                           r->lock->stage_dir(),
                           r->lock->install_dir(),
                           r->spec->identity,
                           eng,
                           r);
  } else {
    throw std::runtime_error("build field must be nil, string, or function for " +
                             r->spec->identity);
  }
}

}  // namespace envy

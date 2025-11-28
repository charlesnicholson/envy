#include "phase_stage.h"

#include "cache.h"
#include "engine.h"
#include "extract.h"
#include "lua_ctx/lua_ctx_bindings.h"
#include "lua_envy.h"
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
  sol::object stage_obj{ lua["stage"] };
  sol::object build_obj{ lua["build"] };
  sol::object install_obj{ lua["install"] };

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

stage_options parse_stage_options(sol::state_view lua, std::string const &key) {
  stage_options opts;

  lua_getfield(lua, -1, "strip");
  if (lua_isnumber(lua, -1)) {
    opts.strip_components = static_cast<int>(lua_tointeger(lua, -1));
    if (opts.strip_components < 0) {
      lua_pop(lua, 2);
      throw std::runtime_error("stage.strip must be non-negative for " + key);
    }
  } else if (!lua_isnil(lua, -1)) {
    lua_pop(lua, 2);
    throw std::runtime_error("stage.strip must be a number for " + key);
  }
  lua_pop(lua, 1);  // Pop strip field

  return opts;
}

void run_default_stage(std::filesystem::path const &fetch_dir,
                       std::filesystem::path const &dest_dir) {
  tui::debug("phase stage: no stage field, running default extraction");
  extract_all_archives(fetch_dir, dest_dir, 0);
}

void run_declarative_stage(sol::state_view lua,
                           std::filesystem::path const &fetch_dir,
                           std::filesystem::path const &dest_dir,
                           std::string const &identity) {
  stage_options const opts{ parse_stage_options(lua, identity) };
  lua_pop(lua.lua_state(), 1);  // Pop stage table

  tui::debug("phase stage: declarative extraction with strip=%d", opts.strip_components);
  extract_all_archives(fetch_dir, dest_dir, opts.strip_components);
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

  sol::protected_function_result result{ stage_func(ctx_table, opts) };

  if (!result.valid()) {
    sol::error err{ result };
    throw std::runtime_error("Stage function failed for " + identity + ": " + err.what());
  }
}

void run_shell_stage(std::string_view script,
                     std::filesystem::path const &dest_dir,
                     std::string const &identity) {
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
                     .shell = shell_parse_choice(std::nullopt) };

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
    return;
  }

  std::string const &identity{ r->spec->identity };
  sol::state_view lua_view{ *r->lua };
  std::filesystem::path const dest_dir{ determine_stage_destination(lua_view, lock) };
  std::filesystem::path const fetch_dir{ lock->fetch_dir() };

  sol::object stage_obj{ lua_view["stage"] };

  if (!stage_obj.valid()) {
    run_default_stage(fetch_dir, dest_dir);
  } else if (stage_obj.is<std::string>()) {
    auto const script_str{ stage_obj.as<std::string>() };
    run_shell_stage(script_str, dest_dir, identity);
  } else if (stage_obj.is<sol::protected_function>()) {
    run_programmatic_stage(stage_obj.as<sol::protected_function>(),
                           fetch_dir,
                           dest_dir,
                           identity,
                           eng,
                           r);
  } else if (stage_obj.is<sol::table>()) {
    stage_obj.push(lua_view.lua_state());
    run_declarative_stage(lua_view, fetch_dir, dest_dir, identity);
  } else {
    throw std::runtime_error("stage field must be nil, string, table, or function for " +
                             identity);
  }
}

}  // namespace envy

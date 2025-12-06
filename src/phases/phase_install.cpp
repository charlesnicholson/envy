#include "phase_install.h"

#include "cache.h"
#include "engine.h"
#include "lua_ctx/lua_ctx_bindings.h"
#include "lua_envy.h"
#include "lua_error_formatter.h"
#include "phase_check.h"
#include "recipe.h"
#include "shell.h"
#include "trace.h"
#include "tui.h"

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace envy {
namespace {

struct install_phase_ctx : lua_ctx_common {
  std::filesystem::path install_dir;
  std::filesystem::path stage_dir;
  std::filesystem::path tmp_dir;
  cache::scoped_entry_lock *lock{ nullptr };
};

bool directory_has_entries(std::filesystem::path const &dir) {
  std::error_code ec;
  if (!std::filesystem::exists(dir, ec) || ec) { return false; }

  std::filesystem::directory_iterator it{ dir, ec };
  if (ec) {
    throw std::runtime_error("Failed to enumerate directory " + dir.string() + ": " +
                             ec.message());
  }

  std::filesystem::directory_iterator end_iter;
  for (; it != end_iter; ++it) { return true; }

  return false;
}

// Build cache-managed install context table (full access to all directories and APIs)
sol::table build_install_phase_ctx_table(sol::state_view lua,
                                         std::string const &identity,
                                         install_phase_ctx *ctx) {
  sol::table ctx_table{ lua.create_table() };

  ctx_table["identity"] = identity;

  ctx_table["fetch_dir"] = ctx->fetch_dir.string();
  ctx_table["stage_dir"] = ctx->stage_dir.string();
  ctx_table["install_dir"] = ctx->install_dir.string();
  ctx_table["mark_install_complete"] = [ctx]() { ctx->lock->mark_install_complete(); };

  lua_ctx_add_common_bindings(ctx_table, ctx);
  return ctx_table;
}

// Build user-managed install phase context table (restricted access)
// Exposes: tmp_dir, run(), options, identity, asset()
// Hides: fetch_dir, stage_dir, build_dir, install_dir, asset_dir,
//        fetch(), extract_all(), mark_install_complete()
sol::table build_user_managed_install_ctx_table(sol::state_view lua,
                                                std::string const &identity,
                                                install_phase_ctx *ctx) {
  sol::table ctx_table{ lua.create_table() };

  ctx_table["identity"] = identity;
  ctx_table["tmp_dir"] = ctx->tmp_dir.string();
  ctx_table["run"] = make_ctx_run(ctx);
  ctx_table["asset"] = make_ctx_asset(ctx);
  ctx_table["product"] = make_ctx_product(ctx);

  auto const forbidden_error{ [identity](std::string const name) {
    return [identity, name]() {
      throw std::runtime_error("ctx." + name +
                               " is not available for user-managed package " + identity +
                               ". User-managed packages cannot use cache-managed APIs.");
    };
  } };

  ctx_table["fetch_dir"] = forbidden_error("fetch_dir");
  ctx_table["stage_dir"] = forbidden_error("stage_dir");
  ctx_table["build_dir"] = forbidden_error("build_dir");
  ctx_table["install_dir"] = forbidden_error("install_dir");
  ctx_table["asset_dir"] = forbidden_error("asset_dir");
  ctx_table["fetch"] = forbidden_error("fetch");
  ctx_table["extract"] = forbidden_error("extract");
  ctx_table["extract_all"] = forbidden_error("extract_all");
  ctx_table["copy"] = forbidden_error("copy");
  ctx_table["move"] = forbidden_error("move");
  ctx_table["ls"] = forbidden_error("ls");
  ctx_table["commit_fetch"] = forbidden_error("commit_fetch");

  // Note: mark_install_complete is NOT exposed at all for user-managed packages
  // (not even as an error lambda) - it simply doesn't exist in the context

  return ctx_table;
}

bool run_shell_install(std::string_view script,
                       std::filesystem::path const &install_dir,
                       cache::scoped_entry_lock *lock,
                       std::string const &identity,
                       resolved_shell shell) {
  tui::debug("phase install: running shell script");

  shell_env_t env{ shell_getenv() };
  shell_run_cfg const cfg{ .on_output_line =
                               [&](std::string_view line) {
                                 tui::info("%.*s",
                                           static_cast<int>(line.size()),
                                           line.data());
                               },
                           .cwd = install_dir,
                           .env = std::move(env),
                           .shell = shell };

  shell_result const result{ shell_run(script, cfg) };

  if (result.exit_code != 0) {
    if (result.signal) {
      throw std::runtime_error("Install shell script terminated by signal " +
                               std::to_string(*result.signal) + " for " + identity);
    }
    throw std::runtime_error("Install shell script failed for " + identity +
                             " (exit code " + std::to_string(result.exit_code) + ")");
  }

  if (lock) {
    lock->mark_install_complete();
    return true;
  }

  return false;
}

bool run_programmatic_install(sol::protected_function install_func,
                              cache::scoped_entry_lock *lock,
                              std::filesystem::path const &fetch_dir,
                              std::filesystem::path const &stage_dir,
                              std::filesystem::path const &install_dir,
                              std::string const &identity,
                              engine &eng,
                              recipe *r,
                              bool is_user_managed) {
  tui::debug("phase install: running programmatic install function");

  // User-managed packages use manifest dir as cwd, cache-managed use install_dir
  std::filesystem::path const run_cwd{ is_user_managed
                                           ? recipe_spec::compute_project_root(r->spec)
                                           : install_dir };

  install_phase_ctx ctx{};
  ctx.fetch_dir = fetch_dir;
  ctx.run_dir = run_cwd;
  ctx.engine_ = &eng;
  ctx.recipe_ = r;
  ctx.install_dir = install_dir;
  ctx.stage_dir = stage_dir;
  ctx.tmp_dir = lock->tmp_dir();
  ctx.lock = lock;

  sol::state_view lua{ install_func.lua_state() };
  sol::table ctx_table{ is_user_managed
                            ? build_user_managed_install_ctx_table(lua, identity, &ctx)
                            : build_install_phase_ctx_table(lua, identity, &ctx) };

  sol::object opts{ lua.registry()[ENVY_OPTIONS_RIDX] };
  sol::object result_obj{ call_lua_function_with_enriched_errors(r, "install", [&]() {
    return install_func(ctx_table, opts);
  }) };

  // Validate return type: must be nil or string
  sol::type const result_type{ result_obj.get_type() };
  if (result_type != sol::type::none && result_type != sol::type::lua_nil) {
    if (!result_obj.is<std::string>()) {
      throw std::runtime_error("install function for " + identity +
                               " must return nil or string, got " +
                               sol::type_name(lua.lua_state(), result_type));
    }

    // Returned string: spawn fresh shell with manifest defaults
    std::string const returned_script{ result_obj.as<std::string>() };
    tui::debug("phase install: running returned string from install function");

    // Returned strings from user-managed packages should error (auto-mark complete)
    if (is_user_managed) {
      throw std::runtime_error(
          "Recipe " + identity +
          " has check verb (user-managed) but install function returned a string. "
          "Returned strings automatically called mark_install_complete(). "
          "Use ctx.run() directly instead, or remove check verb.");
    }

    bool const shell_marked_complete{ run_shell_install(
        returned_script,
        install_dir,
        lock,
        identity,
        shell_resolve_default(r->default_shell_ptr)) };

    return shell_marked_complete || lock->is_install_complete();
  }

  return lock->is_install_complete();
}

bool promote_stage_to_install(cache::scoped_entry_lock *lock) {
  auto const install_dir{ lock->install_dir() };
  auto const stage_dir{ lock->stage_dir() };

  if (directory_has_entries(install_dir)) {
    tui::debug("phase install: install_dir already populated, marking complete");
    lock->mark_install_complete();
    return true;
  }

  if (directory_has_entries(stage_dir)) {
    tui::debug("phase install: promoting stage_dir contents to install_dir");
    std::filesystem::remove_all(install_dir);
    std::filesystem::create_directories(install_dir.parent_path());
    std::filesystem::rename(stage_dir, install_dir);
    lock->mark_install_complete();
    return true;
  }

  tui::debug("phase install: no outputs detected, leaving entry unmarked");
  return false;
}

}  // namespace

void run_install_phase(recipe *r, engine &eng) {
  phase_trace_scope const phase_scope{ r->spec->identity,
                                       recipe_phase::asset_install,
                                       std::chrono::steady_clock::now() };

  if (!r->lock) {  // Cache hit - no work to do
    tui::debug("phase install: no lock (cache hit), skipping");
    return;
  }

  cache::scoped_entry_lock::ptr_t lock{ std::move(r->lock) };
  std::filesystem::path const final_asset_path{ lock->install_dir().parent_path() /
                                                "asset" };

  sol::state_view lua_view{ *r->lua };
  sol::object install_obj{ lua_view["install"] };
  bool marked_complete{ false };

  bool const is_user_managed{ recipe_has_check_verb(r, lua_view) };

  if (!install_obj.valid()) {
    marked_complete = promote_stage_to_install(lock.get());
  } else if (install_obj.is<std::string>()) {
    // String installs: run command, mark complete only if cache-managed
    // User-managed packages use manifest dir as cwd, cache-managed use install_dir
    std::filesystem::path const string_cwd{
      is_user_managed ? recipe_spec::compute_project_root(r->spec) : lock->install_dir()
    };
    std::string script{ install_obj.as<std::string>() };
    marked_complete = run_shell_install(script,
                                        string_cwd,
                                        is_user_managed ? nullptr : lock.get(),
                                        r->spec->identity,
                                        shell_resolve_default(r->default_shell_ptr));
  } else if (install_obj.is<sol::protected_function>()) {
    marked_complete = run_programmatic_install(install_obj.as<sol::protected_function>(),
                                               lock.get(),
                                               lock->fetch_dir(),
                                               lock->stage_dir(),
                                               lock->install_dir(),
                                               r->spec->identity,
                                               eng,
                                               r,
                                               is_user_managed);
  } else {
    throw std::runtime_error("install field must be nil, string, or function for " +
                             r->spec->identity);
  }

  // User-managed packages cannot call mark_install_complete() because it's not exposed
  // in their context table, so no runtime validation needed - enforced at API level

  if (marked_complete) { r->asset_path = final_asset_path; }
}

}  // namespace envy

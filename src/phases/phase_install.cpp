#include "phase_install.h"

#include "cache.h"
#include "engine.h"
#include "lua_ctx/lua_ctx_bindings.h"
#include "lua_envy.h"
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

sol::table build_install_phase_ctx_table(sol::state_view lua,
                                         std::string const &identity,
                                         install_phase_ctx *ctx) {
  sol::table ctx_table{ lua.create_table() };

  ctx_table["identity"] = identity;

  ctx_table["fetch_dir"] = ctx->fetch_dir.string();
  ctx_table["stage_dir"] = ctx->stage_dir.string();
  ctx_table["install_dir"] = ctx->install_dir.string();
  ctx_table["mark_install_complete"] = [ctx]() {
    if (!ctx->lock) {
      throw std::runtime_error("ctx.mark_install_complete: missing install context");
    }
    ctx->lock->mark_install_complete();
  };

  lua_ctx_add_common_bindings(ctx_table, ctx);
  return ctx_table;
}

bool run_programmatic_install(sol::protected_function install_func,
                              cache::scoped_entry_lock *lock,
                              std::filesystem::path const &fetch_dir,
                              std::filesystem::path const &stage_dir,
                              std::filesystem::path const &install_dir,
                              std::string const &identity,
                              engine &eng,
                              recipe *r) {
  tui::debug("phase install: running programmatic install function");

  install_phase_ctx ctx{};
  ctx.fetch_dir = fetch_dir;
  ctx.run_dir = install_dir;
  ctx.engine_ = &eng;
  ctx.recipe_ = r;
  ctx.install_dir = install_dir;
  ctx.stage_dir = stage_dir;
  ctx.lock = lock;

  sol::state_view lua{ install_func.lua_state() };
  sol::table ctx_table{ build_install_phase_ctx_table(lua, identity, &ctx) };

  sol::object opts{ lua.registry()[ENVY_OPTIONS_RIDX] };
  sol::protected_function_result result{ install_func(ctx_table, opts) };

  if (!result.valid()) {
    sol::error err{ result };
    throw std::runtime_error("Install function failed for " + identity + ": " +
                             err.what());
  }

  return lock->is_install_complete();
}

bool run_shell_install(std::string_view script,
                       std::filesystem::path const &install_dir,
                       cache::scoped_entry_lock *lock,
                       std::string const &identity) {
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
                           .shell = shell_parse_choice(std::nullopt) };

  shell_result const result{ shell_run(script, cfg) };

  if (result.exit_code != 0) {
    if (result.signal) {
      throw std::runtime_error("Install shell script terminated by signal " +
                               std::to_string(*result.signal) + " for " + identity);
    }
    throw std::runtime_error("Install shell script failed for " + identity +
                             " (exit code " + std::to_string(result.exit_code) + ")");
  }

  lock->mark_install_complete();
  return true;
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

  if (!install_obj.valid()) {
    marked_complete = promote_stage_to_install(lock.get());
  } else if (install_obj.is<std::string>()) {
    std::string script{ install_obj.as<std::string>() };
    marked_complete =
        run_shell_install(script, lock->install_dir(), lock.get(), r->spec->identity);
  } else if (install_obj.is<sol::protected_function>()) {
    marked_complete = run_programmatic_install(install_obj.as<sol::protected_function>(),
                                               lock.get(),
                                               lock->fetch_dir(),
                                               lock->stage_dir(),
                                               lock->install_dir(),
                                               r->spec->identity,
                                               eng,
                                               r);
  } else {
    throw std::runtime_error("install field must be nil, string, or function for " +
                             r->spec->identity);
  }

  // Validation: User-managed packages (with check verb) must not call
  // mark_install_complete() This enforces the check XOR cache constraint - packages choose
  // one approach, not both. User-managed packages use check verb to determine state; cache
  // entries are ephemeral workspace. Cache-managed packages have no check verb; artifacts
  // persist in cache with envy-complete marker.
  if (recipe_has_check_verb(r, lua_view) && marked_complete) {
    throw std::runtime_error(
        "Recipe " + r->spec->identity +
        " has check verb (user-managed) "
        "but called mark_install_complete(). User-managed recipes must not "
        "populate cache. Remove check verb or remove mark_install_complete() call.");
  }

  if (marked_complete) { r->asset_path = final_asset_path; }
}

}  // namespace envy

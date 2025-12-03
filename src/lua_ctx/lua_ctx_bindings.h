#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <unordered_set>

#include "sol/sol.hpp"

struct lua_State;

namespace envy {

class engine;
struct manifest;
struct recipe;

// Common context fields that all phase contexts must provide.
// Phase-specific contexts should embed this as their first member.
struct lua_ctx_common {
  std::filesystem::path fetch_dir;
  std::filesystem::path run_dir;  // ctx.run() (phase-specific: tmp_dir, stage_dir, etc.)
  engine *engine_;                // Engine for cache access
  recipe *recipe_;                // Current recipe (for ctx.asset() lookups)
};

// Fetch-phase-specific context (extends lua_ctx_common).
// Used by both recipe_fetch and asset_fetch phases.
struct fetch_phase_ctx : lua_ctx_common {
  std::filesystem::path stage_dir;  // Git repos bypass tmp, go directly here
  std::unordered_set<std::string> used_basenames;  // Collision detection for ctx.fetch()
};

// Add common Lua context bindings to a Sol2 table.
// Adds: copy, move, extract, extract_all, asset, ls, run
// All phases should call this to get the standard context functions.
void lua_ctx_add_common_bindings(sol::table &ctx_table, lua_ctx_common *ctx);

// Factory functions that return lambdas for common context functions.
// Each factory captures the context pointer and returns a lambda that implements
// the corresponding ctx.XXX function behavior.

// ctx.copy(src, dst) - copy file or directory
std::function<void(std::string const &, std::string const &)> make_ctx_copy(
    lua_ctx_common *ctx);

// ctx.move(src, dst) - move/rename file or directory
std::function<void(std::string const &, std::string const &)> make_ctx_move(
    lua_ctx_common *ctx);

// ctx.extract(filename, opts?) - extract single archive with optional strip_components
std::function<int(std::string const &, sol::optional<sol::table>)> make_ctx_extract(
    lua_ctx_common *ctx);

// ctx.extract_all(opts?) - extract all archives in fetch_dir
std::function<void(sol::optional<sol::table>)> make_ctx_extract_all(lua_ctx_common *ctx);

// ctx.asset(identity) -> path - look up dependency asset path
std::function<std::string(std::string const &)> make_ctx_asset(lua_ctx_common *ctx);

// ctx.product(name) -> path or value - look up product from declared product dependency
std::function<std::string(std::string const &)> make_ctx_product(lua_ctx_common *ctx);

// ctx.ls(path) - list directory contents for debugging (prints to TUI)
std::function<void(std::string const &)> make_ctx_ls(lua_ctx_common *ctx);

// ctx.run(script, opts?) - run shell script with optional cwd/env/shell config
std::function<sol::table(sol::object, sol::optional<sol::object>, sol::this_state)>
make_ctx_run(lua_ctx_common *ctx);

// ctx.fetch(url_or_table) - download file(s) to tmp directory
// Returns: basename string or array of basenames
std::function<sol::object(sol::object, sol::this_state)> make_ctx_fetch(
    fetch_phase_ctx *ctx);

// ctx.commit_fetch(filename_or_table) - move from tmp to fetch_dir, optional SHA256
std::function<void(sol::object)> make_ctx_commit_fetch(fetch_phase_ctx *ctx);

// Register fetch-phase bindings (ctx.fetch + ctx.commit_fetch)
// Requires fetch_phase_ctx* as context (extends lua_ctx_common)
// Used by both recipe_fetch and asset_fetch phases
void lua_ctx_bindings_register_fetch_phase(sol::table &ctx_table,
                                           fetch_phase_ctx *context);

// Build complete fetch phase context table with identity, tmp_dir, and all bindings
// Returns a Sol2 table ready for use in fetch functions
// Used by both recipe_fetch and asset_fetch phases
sol::table build_fetch_phase_ctx_table(sol::state_view lua,
                                       std::string const &identity,
                                       fetch_phase_ctx *ctx);

// Check if target_identity is a declared dependency of current recipe
// Used for ctx.asset() validation. Exposed for testing.
bool is_declared_dependency(recipe *r, std::string const &target_identity);

}  // namespace envy

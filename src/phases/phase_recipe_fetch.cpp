#include "phase_recipe_fetch.h"

#include "engine.h"
#include "fetch.h"
#include "lua_ctx/lua_ctx_bindings.h"
#include "lua_envy.h"
#include "recipe.h"
#include "sha256.h"
#include "trace.h"
#include "tui.h"

#include <chrono>
#include <stdexcept>
#include <unordered_set>
#include <vector>

namespace envy {

namespace {

// Find the recipe that owns a given recipe_spec pointer
// Returns nullptr if not found (should only happen for root recipes from manifest)
recipe *find_owning_recipe(engine &eng, recipe_spec const *spec) {
  // Iterate through all recipes to find which one owns this spec
  for (auto const &match : eng.find_matches("")) {  // Empty query matches all
    for (auto const &owned_spec : match->owned_dependency_specs) {
      if (&owned_spec == spec) { return match; }
    }
  }
  return nullptr;
}

void validate_phases(lua_State *lua, std::string const &identity) {
  sol::state_view lua_view{ lua };
  sol::object fetch_obj{ lua_view["fetch"] };

  if (bool const has_fetch{ fetch_obj.is<sol::protected_function>() ||
                            fetch_obj.is<std::string>() || fetch_obj.is<sol::table>() }) {
    return;
  }

  sol::object check_obj{ lua_view["check"] };
  bool const has_check{ check_obj.is<sol::protected_function>() };

  sol::object install_obj{ lua_view["install"] };
  bool const has_install{ install_obj.is<sol::protected_function>() };

  if (!has_check || !has_install) {
    throw std::runtime_error("Recipe must define 'fetch' or both 'check' and 'install': " +
                             identity);
  }
}

}  // namespace

void run_recipe_fetch_phase(recipe *r, engine &eng) {
  recipe_spec const &spec{ *r->spec };
  phase_trace_scope const phase_scope{ spec.identity,
                                       recipe_phase::recipe_fetch,
                                       std::chrono::steady_clock::now() };

  // Build ancestor chain for cycle detection (empty for root recipes)
  std::unordered_set<std::string> ancestors;

  auto lua{ std::make_unique<sol::state>() };
  lua->open_libraries(sol::lib::base,
                      sol::lib::package,
                      sol::lib::coroutine,
                      sol::lib::string,
                      sol::lib::os,
                      sol::lib::math,
                      sol::lib::table,
                      sol::lib::debug,
                      sol::lib::bit32,
                      sol::lib::io);
  lua_envy_install(*lua);

  std::filesystem::path recipe_path;
  if (auto const *local_src{ std::get_if<recipe_spec::local_source>(&spec.source) }) {
    recipe_path = local_src->file_path;

    if (sol::protected_function_result result{
            lua->safe_script_file(recipe_path.string(), sol::script_pass_on_error) };
        !result.valid()) {
      sol::error err{ result };
      throw std::runtime_error("Failed to load recipe: " + spec.identity + ": " +
                               err.what());
    }
  } else if (auto const *remote_src{
                 std::get_if<recipe_spec::remote_source>(&spec.source) }) {
    auto cache_result{ r->cache_ptr->ensure_recipe(spec.identity) };

    if (cache_result.lock) {
      tui::debug("fetch recipe %s from %s",
                 spec.identity.c_str(),
                 remote_src->url.c_str());
      std::filesystem::path fetch_dest{ cache_result.lock->install_dir() / "recipe.lua" };

      // Determine fetch request type based on URL scheme
      auto const info{ uri_classify(remote_src->url) };
      fetch_request req;
      switch (info.scheme) {
        case uri_scheme::HTTP:
          req = fetch_request_http{ .source = remote_src->url, .destination = fetch_dest };
          break;
        case uri_scheme::HTTPS:
          req =
              fetch_request_https{ .source = remote_src->url, .destination = fetch_dest };
          break;
        case uri_scheme::FTP:
          req = fetch_request_ftp{ .source = remote_src->url, .destination = fetch_dest };
          break;
        case uri_scheme::FTPS:
          req = fetch_request_ftps{ .source = remote_src->url, .destination = fetch_dest };
          break;
        case uri_scheme::S3:
          req = fetch_request_s3{ .source = remote_src->url, .destination = fetch_dest };
          break;
        case uri_scheme::LOCAL_FILE_ABSOLUTE:
        case uri_scheme::LOCAL_FILE_RELATIVE:
          req = fetch_request_file{ .source = remote_src->url, .destination = fetch_dest };
          break;
        default:
          throw std::runtime_error("Unsupported URL scheme for recipe fetch: " +
                                   remote_src->url);
      }

      auto const results{ fetch({ req }) };
      if (results.empty() || std::holds_alternative<std::string>(results[0])) {
        throw std::runtime_error(
            "Failed to fetch recipe: " +
            (results.empty() ? "no results" : std::get<std::string>(results[0])));
      }

      if (!remote_src->sha256.empty()) {
        tui::debug("verifying SHA256 for recipe %s", spec.identity.c_str());
        sha256_verify(remote_src->sha256, sha256(fetch_dest));
      }

      cache_result.lock->mark_install_complete();
      cache_result.lock.reset();
    }

    recipe_path = cache_result.asset_path / "recipe.lua";

    if (sol::protected_function_result result{
            lua->safe_script_file(recipe_path.string(), sol::script_pass_on_error) };
        !result.valid()) {
      sol::error err{ result };
      throw std::runtime_error("Failed to load recipe: " + spec.identity + ": " +
                               err.what());
    }
  } else if (auto const *git_src{ std::get_if<recipe_spec::git_source>(&spec.source) }) {
    auto cache_result{ r->cache_ptr->ensure_recipe(spec.identity) };

    if (cache_result.lock) {
      tui::debug("fetch recipe %s from git %s @ %s",
                 spec.identity.c_str(),
                 git_src->url.c_str(),
                 git_src->ref.c_str());

      // For git sources, clone directly to the install directory (no extraction needed)
      std::filesystem::path install_dir{ cache_result.lock->install_dir() };

      auto const results{ fetch({ fetch_request_git{ .source = git_src->url,
                                                     .destination = install_dir,
                                                     .ref = git_src->ref } }) };
      if (results.empty() || std::holds_alternative<std::string>(results[0])) {
        throw std::runtime_error(
            "Failed to fetch git recipe: " +
            (results.empty() ? "no results" : std::get<std::string>(results[0])));
      }

      cache_result.lock->mark_install_complete();
      cache_result.lock.reset();
    }

    recipe_path = cache_result.asset_path / "recipe.lua";
    sol::protected_function_result result =
        lua->safe_script_file(recipe_path.string(), sol::script_pass_on_error);
    if (!result.valid()) {
      sol::error err = result;
      throw std::runtime_error("Failed to load recipe: " + spec.identity + ": " +
                               err.what());
    }
  } else if (spec.has_fetch_function()) {
    // Custom fetch with source dependencies
    // Find the owning recipe (parent) that defined this dependency
    recipe *parent{ find_owning_recipe(eng, r->spec) };
    if (!parent || !parent->lua) {
      throw std::runtime_error(
          "Custom fetch function recipe has no parent Lua state: " + spec.identity);
    }

    // Get cache entry for this recipe
    auto cache_result{ r->cache_ptr->ensure_recipe(spec.identity) };

    if (cache_result.lock) {
      tui::debug("fetch recipe %s via custom fetch function", spec.identity.c_str());

      // Create fetch_phase_ctx for custom fetch function
      fetch_phase_ctx ctx;
      ctx.fetch_dir = cache_result.lock->install_dir();
      ctx.run_dir = cache_result.lock->work_dir() / "tmp";
      ctx.stage_dir = cache_result.lock->stage_dir();
      ctx.engine_ = &eng;
      ctx.recipe_ = r;

      // Create tmp directory for downloads
      std::filesystem::create_directories(ctx.run_dir);

      // Build Lua context table with all fetch bindings
      lua_State *parent_lua{ parent->lua->lua_state() };
      sol::state_view parent_lua_view{ parent_lua };
      sol::table ctx_table{ build_fetch_phase_ctx_table(parent_lua_view, spec.identity, &ctx) };

      // Look up the fetch function from parent's dependencies
      if (!recipe_spec::lookup_and_push_source_fetch(parent_lua, spec.identity)) {
        throw std::runtime_error("Failed to lookup fetch function for: " + spec.identity);
      }

      // Stack: [function]
      // Get options from registry
      lua_rawgeti(parent_lua, LUA_REGISTRYINDEX, ENVY_OPTIONS_RIDX);
      // Stack: [function, options]

      // Call fetch(ctx, options)
      sol::protected_function fetch_func{ parent_lua_view, sol::stack_reference(parent_lua, -2) };
      sol::object options_obj{ parent_lua_view, sol::stack_reference(parent_lua, -1) };

      sol::protected_function_result fetch_result{ fetch_func(ctx_table, options_obj) };
      lua_pop(parent_lua, 2);  // Pop function and options

      if (!fetch_result.valid()) {
        sol::error err{ fetch_result };
        throw std::runtime_error("Fetch function failed for " + spec.identity + ": " +
                                 err.what());
      }

      cache_result.lock->mark_install_complete();
      cache_result.lock.reset();
    }

    // Load the recipe.lua that was created by custom fetch
    recipe_path = cache_result.asset_path / "recipe.lua";
    if (!std::filesystem::exists(recipe_path)) {
      throw std::runtime_error("Custom fetch did not create recipe.lua for: " + spec.identity);
    }

    sol::protected_function_result result =
        lua->safe_script_file(recipe_path.string(), sol::script_pass_on_error);
    if (!result.valid()) {
      sol::error err = result;
      throw std::runtime_error("Failed to load recipe after custom fetch: " + spec.identity +
                               ": " + err.what());
    }
  } else {
    throw std::runtime_error("Unsupported source type: " + spec.identity);
  }

  std::string const declared_identity{ [&] {
    try {
      sol::object identity_obj = (*lua)["identity"];
      if (!identity_obj.valid() || identity_obj.get_type() != sol::type::string) {
        throw std::runtime_error("Recipe must define 'identity' global as a string");
      }
      return identity_obj.as<std::string>();
    } catch (std::runtime_error const &e) {
      throw std::runtime_error(std::string(e.what()) + " (in recipe: " + spec.identity +
                               ")");
    }
  }() };

  if (declared_identity != spec.identity) {
    throw std::runtime_error("Identity mismatch: expected '" + spec.identity +
                             "' but recipe declares '" + declared_identity + "'");
  }

  validate_phases(lua->lua_state(), spec.identity);

  // Parse dependencies and move into recipe's owned storage
  sol::object deps_obj{ (*lua)["dependencies"] };
  if (deps_obj.valid() && deps_obj.get_type() == sol::type::table) {
    sol::table deps_table{ deps_obj.as<sol::table>() };
    lua_State *L{ lua->lua_state() };

    for (size_t i{ 1 }; i <= deps_table.size(); ++i) {
      deps_table[i].push(L);
      auto dep_cfg{ recipe_spec::parse_from_stack(L, -1, recipe_path) };
      lua_pop(L, 1);

      if (!spec.identity.starts_with("local.") && dep_cfg.identity.starts_with("local.")) {
        throw std::runtime_error("Security violation: non-local recipe '" + spec.identity +
                                 "' cannot depend on local recipe '" + dep_cfg.identity +
                                 "'");
      }

      r->owned_dependency_specs.push_back(std::move(dep_cfg));
    }
  }

  // Extract dependency identities for validation
  std::vector<std::string> const dep_identities{ [&]() {
    std::vector<std::string> result;
    result.reserve(r->owned_dependency_specs.size());
    for (auto const &dep_spec : r->owned_dependency_specs) {
      result.push_back(dep_spec.identity);
    }
    return result;
  }() };

  // Deserialize options into Lua registry for phase functions
  lua_State *L{ lua->lua_state() };
  sol::protected_function_result opts_result{
    lua->safe_script("return " + spec.serialized_options, sol::script_pass_on_error)
  };
  if (!opts_result.valid()) {
    sol::error err{ opts_result };
    throw std::runtime_error("Failed to deserialize options for " + spec.identity + ": " +
                             err.what());
  }
  opts_result.get<sol::object>().push(L);
  lua_rawseti(L, LUA_REGISTRYINDEX, ENVY_OPTIONS_RIDX);

  r->lua = std::move(lua);
  r->declared_dependencies = std::move(dep_identities);

  // Get ancestor chain from execution context (per-thread traversal state)
  auto &ctx{ eng.get_execution_ctx(r) };
  std::vector<std::string> const &ancestor_chain{ ctx.ancestor_chain };

  // Build dependency graph: create child recipes, start their threads
  for (auto &dep_spec : r->owned_dependency_specs) {
    // Cycle detection: check for self-loops and cycles in ancestor chain
    if (r->spec->identity == dep_spec.identity) {
      throw std::runtime_error("Dependency cycle detected: " + r->spec->identity + " -> " +
                               dep_spec.identity);
    }

    for (auto const &ancestor : ancestor_chain) {
      if (ancestor == dep_spec.identity) {
        // Build error message with cycle path
        std::string cycle_path{ ancestor };
        bool found_start{ false };
        for (auto const &a : ancestor_chain) {
          if (a == ancestor) { found_start = true; }
          if (found_start) { cycle_path += " -> " + a; }
        }
        cycle_path += " -> " + dep_spec.identity;
        throw std::runtime_error("Dependency cycle detected: " + cycle_path);
      }
    }

    recipe_phase const needed_by_phase{
      dep_spec.needed_by.has_value() ? static_cast<recipe_phase>(*dep_spec.needed_by)
                                     : recipe_phase::asset_build
    };

    recipe *dep{ eng.ensure_recipe(&dep_spec) };

    // Build child ancestor chain (local to this thread path)
    std::vector<std::string> child_chain{ ancestor_chain };
    child_chain.push_back(r->spec->identity);

    // Store dependency info in parent's map for ctx.asset() lookup and phase coordination
    r->dependencies[dep_spec.identity] = { dep, needed_by_phase };
    ENVY_TRACE_DEPENDENCY_ADDED(r->spec->identity, dep_spec.identity, needed_by_phase);

    eng.start_recipe_thread(dep, recipe_phase::recipe_fetch, std::move(child_chain));
  }

  eng.on_recipe_fetch_complete();
}

}  // namespace envy

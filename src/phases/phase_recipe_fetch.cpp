#include "phase_recipe_fetch.h"

#include "engine.h"
#include "fetch.h"
#include "lua_ctx/lua_ctx_bindings.h"
#include "lua_envy.h"
#include "recipe.h"
#include "sha256.h"
#include "sol_util.h"
#include "trace.h"
#include "tui.h"

#include <chrono>
#include <stdexcept>
#include <vector>

namespace envy {

namespace {

void validate_phases(sol::state_view lua, std::string const &identity) {
  sol::object fetch_obj{ lua["fetch"] };

  if (bool const has_fetch{ fetch_obj.is<sol::protected_function>() ||
                            fetch_obj.is<std::string>() || fetch_obj.is<sol::table>() }) {
    return;
  }

  bool const has_check{ lua["check"].is<sol::protected_function>() };
  bool const has_install{ lua["install"].is<sol::protected_function>() };

  if (!has_check || !has_install) {
    throw std::runtime_error("Recipe must define 'fetch' or both 'check' and 'install': " +
                             identity);
  }
}

sol_state_ptr create_lua_state() {
  auto lua{ sol_util_make_lua_state() };
  lua_envy_install(*lua);
  return lua;
}

void load_recipe_script(sol::state &lua,
                        std::filesystem::path const &recipe_path,
                        std::string const &identity) {
  sol::protected_function_result result{ lua.safe_script_file(recipe_path.string(),
                                                              sol::script_pass_on_error) };
  if (!result.valid()) {
    sol::error err{ result };
    throw std::runtime_error("Failed to load recipe: " + identity + ": " + err.what());
  }
}

std::filesystem::path get_cached_recipe_path(recipe const *r) {
  return r->cache_ptr->ensure_recipe(r->spec->identity).asset_path / "recipe.lua";
}

std::filesystem::path fetch_local_source(recipe_spec const &spec) {
  auto const *local_src{ std::get_if<recipe_spec::local_source>(&spec.source) };
  return local_src->file_path;
}

std::filesystem::path fetch_remote_source(recipe_spec const &spec, recipe *r) {
  auto const *remote_src{ std::get_if<recipe_spec::remote_source>(&spec.source) };
  auto cache_result{ r->cache_ptr->ensure_recipe(spec.identity) };

  if (cache_result.lock) {
    tui::debug("fetch recipe %s from %s", spec.identity.c_str(), remote_src->url.c_str());
    std::filesystem::path fetch_dest{ cache_result.lock->install_dir() / "recipe.lua" };

    // Determine fetch request type based on URL scheme
    auto const info{ uri_classify(remote_src->url) };
    fetch_request req;
    switch (info.scheme) {
      case uri_scheme::HTTP:
        req = fetch_request_http{ .source = remote_src->url, .destination = fetch_dest };
        break;
      case uri_scheme::HTTPS:
        req = fetch_request_https{ .source = remote_src->url, .destination = fetch_dest };
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

  return cache_result.asset_path / "recipe.lua";
}

std::filesystem::path fetch_git_source(recipe_spec const &spec, recipe *r) {
  auto const *git_src{ std::get_if<recipe_spec::git_source>(&spec.source) };
  auto cache_result{ r->cache_ptr->ensure_recipe(spec.identity) };

  if (cache_result.lock) {
    tui::debug("fetch recipe %s from git %s @ %s",
               spec.identity.c_str(),
               git_src->url.c_str(),
               git_src->ref.c_str());

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

  return cache_result.asset_path / "recipe.lua";
}

std::filesystem::path fetch_custom_function(recipe_spec const &spec,
                                            recipe *r,
                                            engine &eng) {
  if (!spec.parent) {
    throw std::runtime_error("Custom fetch function recipe has no parent: " +
                             spec.identity);
  }

  recipe *parent{ eng.find_exact(recipe_key(*spec.parent)) };
  if (!parent) {
    throw std::runtime_error("Custom fetch function recipe parent not found: " +
                             spec.identity);
  }

  {  // Check parent lua under lock
    std::lock_guard const lock(parent->lua_mutex);
    if (!parent->lua) {
      throw std::runtime_error("Custom fetch function recipe has no parent Lua state: " +
                               spec.identity);
    }
  }

  auto cache_result{ r->cache_ptr->ensure_recipe(spec.identity) };

  if (cache_result.lock) {
    tui::debug("fetch recipe %s via custom fetch function", spec.identity.c_str());

    // Create fetch_phase_ctx for custom fetch function
    fetch_phase_ctx ctx;
    ctx.fetch_dir = cache_result.lock->install_dir();
    ctx.run_dir = cache_result.lock->work_dir() / "tmp";
    ctx.stage_dir = cache_result.lock->stage_dir();
    ctx.engine_ = &eng;
    ctx.recipe_ = parent;

    std::filesystem::create_directories(ctx.run_dir);

    // Build Lua context table with all fetch bindings
    sol::state_view parent_lua_view{ *parent->lua };
    sol::table ctx_table{
      build_fetch_phase_ctx_table(parent_lua_view, spec.identity, &ctx)
    };

    // Look up the fetch function from parent's dependencies
    if (!recipe_spec::lookup_and_push_source_fetch(parent_lua_view, spec.identity)) {
      throw std::runtime_error("Failed to lookup fetch function for: " + spec.identity);
    }

    // Stack: [function]
    // Call fetch(ctx, options)
    sol::protected_function fetch_func{ parent_lua_view,
                                        sol::stack_reference(parent_lua_view.lua_state(),
                                                             -1) };
    lua_pop(parent_lua_view.lua_state(), 1);  // pop function
    sol::object options_obj{ parent_lua_view.registry()[ENVY_OPTIONS_RIDX] };

    sol::protected_function_result fetch_result{ fetch_func(ctx_table, options_obj) };

    if (!fetch_result.valid()) {
      sol::error err{ fetch_result };
      throw std::runtime_error("Fetch function failed for " + spec.identity + ": " +
                               err.what());
    }

    cache_result.lock->mark_install_complete();
    cache_result.lock.reset();
  }

  // Validate the recipe.lua that was created by custom fetch
  std::filesystem::path const recipe_path{ cache_result.asset_path / "recipe.lua" };
  if (!std::filesystem::exists(recipe_path)) {
    throw std::runtime_error("Custom fetch did not create recipe.lua for: " +
                             spec.identity);
  }

  return recipe_path;
}

}  // namespace

void run_recipe_fetch_phase(recipe *r, engine &eng) {
  recipe_spec const &spec{ *r->spec };
  phase_trace_scope const phase_scope{ spec.identity,
                                       recipe_phase::recipe_fetch,
                                       std::chrono::steady_clock::now() };

  std::filesystem::path const recipe_path{ [&] {  // Fetch recipe based on source type
    if (auto const *local_src{ std::get_if<recipe_spec::local_source>(&spec.source) }) {
      return fetch_local_source(spec);
    } else if (std::holds_alternative<recipe_spec::remote_source>(spec.source)) {
      return fetch_remote_source(spec, r);
    } else if (std::holds_alternative<recipe_spec::git_source>(spec.source)) {
      return fetch_git_source(spec, r);
    } else if (spec.has_fetch_function()) {
      return fetch_custom_function(spec, r, eng);
    } else {
      throw std::runtime_error("Unsupported source type: " + spec.identity);
    }
  }() };

  auto lua{ create_lua_state() };
  load_recipe_script(*lua, recipe_path, spec.identity);

  std::string const declared_identity{ [&] {
    try {
      sol::object identity_obj{ (*lua)["identity"] };
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

  validate_phases(sol::state_view{ *lua }, spec.identity);

  r->products = [&] {  // Parse products table from Lua global
    std::unordered_map<std::string, std::string> parsed_products;
    sol::object products_obj{ (*lua)["products"] };
    if (products_obj.valid()) {
      if (products_obj.get_type() != sol::type::table) {
        throw std::runtime_error("products must be table in recipe '" + spec.identity +
                                 "'");
      }
      sol::table products_table{ products_obj.as<sol::table>() };
      for (auto const &[key, value] : products_table) {
        sol::object key_obj(key);
        sol::object val_obj(value);
        if (!key_obj.is<std::string>()) {
          throw std::runtime_error("products key must be string in recipe '" +
                                   spec.identity + "'");
        }
        if (!val_obj.is<std::string>()) {
          throw std::runtime_error("products value must be string in recipe '" +
                                   spec.identity + "'");
        }
        std::string key_str{ key_obj.as<std::string>() };
        std::string val_str{ val_obj.as<std::string>() };
        if (key_str.empty()) {
          throw std::runtime_error("products key cannot be empty in recipe '" +
                                   spec.identity + "'");
        }
        if (val_str.empty()) {
          throw std::runtime_error("products value cannot be empty in recipe '" +
                                   spec.identity + "'");
        }
        parsed_products[std::move(key_str)] = std::move(val_str);
      }
    }
    return parsed_products;
  }();

  r->owned_dependency_specs = [&] {  // Parse and store dependencies
    std::vector<recipe_spec *> parsed_deps;
    sol::object deps_obj{ (*lua)["dependencies"] };
    if (deps_obj.valid() && deps_obj.get_type() == sol::type::table) {
      sol::table deps_table{ deps_obj.as<sol::table>() };
      for (size_t i{ 1 }; i <= deps_table.size(); ++i) {
        recipe_spec *dep_cfg{ recipe_spec::parse(deps_table[i], recipe_path, true) };

        if (!spec.identity.starts_with("local.") &&
            dep_cfg->identity.starts_with("local.")) {
          throw std::runtime_error("non-local recipe '" + spec.identity +
                                   "' cannot depend on local recipe '" +
                                   dep_cfg->identity + "'");
        }

        dep_cfg->parent = r->spec;
        parsed_deps.push_back(dep_cfg);
      }
    }
    return parsed_deps;
  }();

  lua->registry()[ENVY_OPTIONS_RIDX] = [&] {  // store "options" into Lua registry
    sol::protected_function_result opts_result{
      lua->safe_script("return " + spec.serialized_options, sol::script_pass_on_error)
    };
    if (!opts_result.valid()) {
      sol::error err{ opts_result };
      throw std::runtime_error("Failed to deserialize options for " + spec.identity +
                               ": " + err.what());
    }
    return opts_result.get<sol::object>();
  }();

  {
    std::lock_guard const lock(r->lua_mutex);
    r->lua = std::move(lua);
  }

  r->declared_dependencies = [&]() {  // Extract dependency identities for validation
    std::vector<std::string> result;
    result.reserve(r->owned_dependency_specs.size());
    for (auto const *dep_spec : r->owned_dependency_specs) {
      result.push_back(dep_spec->identity);
    }
    return result;
  }();

  auto &ctx{ eng.get_execution_ctx(r) };

  // Build dependency graph: create child recipes, start their threads
  for (auto *dep_spec : r->owned_dependency_specs) {
    engine_validate_dependency_cycle(dep_spec->identity,
                                     ctx.ancestor_chain,
                                     r->spec->identity,
                                     "Dependency");

    recipe_phase const needed_by_phase{
      dep_spec->needed_by.has_value() ? static_cast<recipe_phase>(*dep_spec->needed_by)
                                      : recipe_phase::asset_build
    };
    bool const is_product_dep{ dep_spec->product.has_value() };

    if (dep_spec->is_weak_reference()) {
      r->weak_references.push_back(recipe::weak_reference{
          .query = is_product_dep ? *dep_spec->product : dep_spec->identity,
          .fallback = dep_spec->weak,
          .needed_by = needed_by_phase,
          .resolved = nullptr,
          .is_product = is_product_dep,
          .constraint_identity = is_product_dep ? dep_spec->identity : "" });
      continue;
    }

    if (is_product_dep) {
      recipe *dep{ eng.ensure_recipe(dep_spec) };

      r->dependencies[dep_spec->identity] = { dep, needed_by_phase };
      ENVY_TRACE_DEPENDENCY_ADDED(r->spec->identity, dep_spec->identity, needed_by_phase);

      std::vector<std::string> child_chain{ ctx.ancestor_chain };
      child_chain.push_back(r->spec->identity);
      eng.start_recipe_thread(dep, recipe_phase::recipe_fetch, std::move(child_chain));

      r->weak_references.push_back(recipe::weak_reference{
          .query = *dep_spec->product,
          .fallback = nullptr,
          .needed_by = needed_by_phase,
          .resolved = nullptr,
          .is_product = true,
          .constraint_identity = dep_spec->identity });
      continue;
    }

    recipe *dep{ eng.ensure_recipe(dep_spec) };

    // Store dependency info in parent's map for ctx.asset() lookup and phase coordination
    r->dependencies[dep_spec->identity] = { dep, needed_by_phase };
    ENVY_TRACE_DEPENDENCY_ADDED(r->spec->identity, dep_spec->identity, needed_by_phase);

    std::vector<std::string> child_chain{ ctx.ancestor_chain };
    child_chain.push_back(r->spec->identity);
    eng.start_recipe_thread(dep, recipe_phase::recipe_fetch, std::move(child_chain));
  }

  eng.on_recipe_fetch_complete();
}

}  // namespace envy

#include "phase_recipe_fetch.h"

#include "engine.h"
#include "fetch.h"
#include "lua_ctx/lua_ctx_bindings.h"
#include "lua_ctx/lua_phase_context.h"
#include "lua_envy.h"
#include "lua_error_formatter.h"
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
  sol::object fetch_obj{ lua["FETCH"] };

  if (bool const has_fetch{ fetch_obj.is<sol::protected_function>() ||
                            fetch_obj.is<std::string>() || fetch_obj.is<sol::table>() }) {
    return;
  }

  bool const has_check{ lua["CHECK"].is<sol::protected_function>() ||
                        lua["CHECK"].is<std::string>() };
  bool const has_install{ lua["INSTALL"].is<sol::protected_function>() ||
                          lua["INSTALL"].is<std::string>() };

  if (!has_check || !has_install) {
    throw std::runtime_error("Recipe must define 'FETCH' or both 'CHECK' and 'INSTALL': " +
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
    sol::error err = result;
    throw std::runtime_error("Failed to load recipe: " + identity + ": " + err.what());
  }

  // Validate user-managed packages (check verb) don't use cache phases
  sol::object check_obj{ lua["CHECK"] };
  bool const has_check_verb{ check_obj.valid() &&
                             (check_obj.is<std::string>() ||
                              check_obj.is<sol::protected_function>()) };

  if (has_check_verb) {
    // User-managed packages cannot use fetch/stage/build phases
    sol::object fetch_obj{ lua["FETCH"] };
    if (fetch_obj.valid() && fetch_obj.is<sol::protected_function>()) {
      throw std::runtime_error(
          "Recipe " + identity +
          " has CHECK verb (user-managed) but declares FETCH phase. "
          "User-managed packages cannot use cache-managed phases (FETCH/STAGE/BUILD). "
          "Remove CHECK verb or remove FETCH phase.");
    }

    sol::object stage_obj{ lua["STAGE"] };
    if (stage_obj.valid() && stage_obj.is<sol::protected_function>()) {
      throw std::runtime_error(
          "Recipe " + identity +
          " has CHECK verb (user-managed) but declares STAGE phase. "
          "User-managed packages cannot use cache-managed phases (FETCH/STAGE/BUILD). "
          "Remove CHECK verb or remove STAGE phase.");
    }

    sol::object build_obj{ lua["BUILD"] };
    if (build_obj.valid() && build_obj.is<sol::protected_function>()) {
      throw std::runtime_error(
          "Recipe " + identity +
          " has CHECK verb (user-managed) but declares BUILD phase. "
          "User-managed packages cannot use cache-managed phases (FETCH/STAGE/BUILD). "
          "Remove CHECK verb or remove BUILD phase.");
    }
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

    std::lock_guard const lock(parent->lua_mutex);

    if (!parent->lua) {
      throw std::runtime_error("Custom fetch function recipe has no parent Lua state: " +
                               spec.identity);
    }

    {
      sol::state_view parent_lua_view{ *parent->lua };

      // Look up the fetch function from parent's dependencies
      if (!recipe_spec::lookup_and_push_source_fetch(parent_lua_view, spec.identity)) {
        throw std::runtime_error("Failed to lookup fetch function for: " + spec.identity);
      }

      // Stack: [function]
      // Call fetch(tmp_dir, options) - new signature matches FETCH phase
      sol::protected_function fetch_func{ parent_lua_view,
                                          sol::stack_reference(parent_lua_view.lua_state(),
                                                               -1) };
      lua_pop(parent_lua_view.lua_state(), 1);  // pop function
      sol::object options_obj{ parent_lua_view.registry()[ENVY_OPTIONS_RIDX] };

      // Temporarily swap parent->lock so envy.commit_fetch can access paths.
      // The inline source.fetch runs in parent's Lua state, so envy.commit_fetch
      // will look up parent from the registry and need parent->lock to be valid.
      std::swap(parent->lock, cache_result.lock);

      // Set up phase context so envy.* functions can find the recipe
      phase_context_guard ctx_guard{ &eng, parent };

      sol::protected_function_result fetch_result{
        fetch_func(ctx.run_dir.string(), options_obj)
      };

      std::swap(parent->lock, cache_result.lock);

      if (!fetch_result.valid()) {
        sol::error err = fetch_result;
        throw std::runtime_error("Fetch function failed for " + spec.identity + ": " +
                                 err.what());
      }
    }

    // Custom fetch creates recipe.lua in fetch_dir via envy.commit_fetch.
    // The lock destructor will clean up fetch_dir, so move recipe.lua to install_dir
    // which gets renamed to asset_dir on successful completion.
    std::filesystem::path const fetch_dir{ cache_result.lock->fetch_dir() };
    std::filesystem::path const install_dir{ cache_result.lock->install_dir() };
    std::filesystem::path const recipe_src{ fetch_dir / "recipe.lua" };
    std::filesystem::path const recipe_dst{ install_dir / "recipe.lua" };

    if (!std::filesystem::exists(recipe_src)) {
      throw std::runtime_error("Custom fetch did not create recipe.lua for: " +
                               spec.identity);
    }

    std::filesystem::rename(recipe_src, recipe_dst);

    cache_result.lock->mark_install_complete();
    cache_result.lock.reset();

    // Recipe is now at asset_path / "recipe.lua" after lock cleanup
    return cache_result.asset_path / "recipe.lua";
  }

  // Cache was already complete - recipe.lua should exist in asset_path
  std::filesystem::path const recipe_path{ cache_result.asset_path / "recipe.lua" };
  if (!std::filesystem::exists(recipe_path)) {
    throw std::runtime_error("Custom fetch did not create recipe.lua for: " +
                             spec.identity);
  }

  return recipe_path;
}

std::unordered_map<std::string, std::string> parse_products_table(recipe_spec const &spec,
                                                                  sol::state &lua,
                                                                  recipe *r) {
  std::unordered_map<std::string, std::string> parsed_products;
  sol::object products_obj{ lua["PRODUCTS"] };
  std::string const &id{ spec.identity };

  if (!products_obj.valid()) { return parsed_products; }

  sol::table products_table;

  // Handle programmatic products: function that takes options, returns table
  if (products_obj.get_type() == sol::type::function) {
    sol::function products_fn{ products_obj.as<sol::function>() };

    // Deserialize options from spec to pass to products function
    std::string const opts_str{ "return " + spec.serialized_options };
    auto opts_result{ lua.safe_script(opts_str, sol::script_pass_on_error) };
    if (!opts_result.valid()) {
      sol::error err = opts_result;
      throw std::runtime_error("Failed to deserialize options for PRODUCTS function: " +
                               std::string(err.what()));
    }
    sol::object options{ opts_result.get<sol::object>() };

    // Call products(options) with enriched error handling
    sol::protected_function_result result{ call_lua_function_with_enriched_errors(
        r,
        "PRODUCTS",
        [&]() { return sol::protected_function_result{ products_fn(options) }; }) };

    sol::object result_obj{ result };
    if (result_obj.get_type() != sol::type::table) {
      throw std::runtime_error("PRODUCTS function must return table in recipe '" + id +
                               "'");
    }
    products_table = result_obj.as<sol::table>();
  } else if (products_obj.get_type() == sol::type::table) {
    products_table = products_obj.as<sol::table>();
  } else {
    throw std::runtime_error("PRODUCTS must be table or function in recipe '" + id + "'");
  }
  sol::object check_obj{ lua["CHECK"] };
  bool const has_check{ check_obj.valid() && check_obj.get_type() == sol::type::function };

  for (auto const &[key, value] : products_table) {
    sol::object key_obj(key);
    sol::object val_obj(value);

    if (!key_obj.is<std::string>()) {
      throw std::runtime_error("PRODUCTS key must be string in recipe '" + id + "'");
    }
    if (!val_obj.is<std::string>()) {
      throw std::runtime_error("PRODUCTS value must be string in recipe '" + id + "'");
    }

    std::string key_str{ key_obj.as<std::string>() };
    std::string val_str{ val_obj.as<std::string>() };

    if (key_str.empty()) {
      throw std::runtime_error("PRODUCTS key cannot be empty in recipe '" + id + "'");
    }
    if (val_str.empty()) {
      throw std::runtime_error("PRODUCTS value cannot be empty in recipe '" + id + "'");
    }

    // Validate path safety for cached recipes (user-managed recipes have arbitrary values)
    if (!has_check) {
      std::filesystem::path product_path{ val_str };

      if (product_path.is_absolute() || (!val_str.empty() && val_str[0] == '/')) {
        throw std::runtime_error("PRODUCTS value '" + val_str +
                                 "' cannot be absolute path in recipe '" + id + "'");
      }

      // Check for path traversal (..) components
      for (auto const &component : product_path) {
        if (component == "..") {
          throw std::runtime_error("PRODUCTS value '" + val_str +
                                   "' cannot contain path traversal (..) in recipe '" +
                                   id + "'");
        }
      }
    }

    parsed_products[std::move(key_str)] = std::move(val_str);
  }

  return parsed_products;
}

std::vector<recipe_spec *> parse_dependencies_table(
    sol::state &lua,
    std::filesystem::path const &recipe_path,
    recipe_spec const &spec) {
  std::vector<recipe_spec *> parsed_deps;
  sol::object deps_obj{ lua["DEPENDENCIES"] };

  if (!deps_obj.valid() || deps_obj.get_type() != sol::type::table) { return parsed_deps; }

  sol::table deps_table{ deps_obj.as<sol::table>() };
  for (size_t i{ 1 }; i <= deps_table.size(); ++i) {
    recipe_spec *dep_cfg{ recipe_spec::parse(deps_table[i], recipe_path, true) };

    if (!spec.identity.starts_with("local.") && dep_cfg->identity.starts_with("local.")) {
      throw std::runtime_error("non-local recipe '" + spec.identity +
                               "' cannot depend on local recipe '" + dep_cfg->identity +
                               "'");
    }

    parsed_deps.push_back(dep_cfg);
  }

  return parsed_deps;
}

sol::object store_options_in_registry(sol::state &lua,
                                      std::string const &serialized_options) {
  sol::protected_function_result opts_result{
    lua.safe_script("return " + serialized_options, sol::script_pass_on_error)
  };

  if (!opts_result.valid()) {
    sol::error err = opts_result;
    throw std::runtime_error("Failed to deserialize options: " + std::string(err.what()));
  }

  return opts_result.get<sol::object>();
}

void run_validate(recipe *r, sol::state &lua) {
  sol::table globals{ lua.globals() };
  std::optional<sol::protected_function> validate_fn;
  try {
    validate_fn =
        sol_util_get_optional<sol::protected_function>(globals, "VALIDATE", "Recipe");
  } catch (std::runtime_error const &e) {
    throw std::runtime_error(std::string(e.what()) + " in recipe '" + r->spec->identity +
                             "'");
  }
  if (!validate_fn.has_value()) { return; }

  sol::object options_obj{ lua.registry()[ENVY_OPTIONS_RIDX] };

  sol::protected_function_result result{ call_lua_function_with_enriched_errors(
      r,
      "validate",
      [&]() { return (*validate_fn)(options_obj); }) };

  sol::object ret_obj{ result };
  sol::type const ret_type{ ret_obj.get_type() };

  auto const failure_prefix{ [&]() {
    return "VALIDATE failed for " + r->spec->format_key();
  } };

  switch (ret_type) {
    case sol::type::lua_nil: return;
    case sol::type::boolean:
      if (ret_obj.as<bool>()) { return; }
      throw std::runtime_error(failure_prefix() + " (returned false)");
    case sol::type::string:
      throw std::runtime_error(failure_prefix() + ": " + ret_obj.as<std::string>());
    default:
      throw std::runtime_error("VALIDATE must return nil/true/false/string (got " +
                               std::string(sol::type_name(lua, ret_type)) + ") for " +
                               r->spec->format_key());
  }
}

void wire_dependency_graph(recipe *r, engine &eng) {
  auto &ctx{ eng.get_execution_ctx(r) };

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

    if (is_product_dep) {
      std::string const &product_name{ *dep_spec->product };

      if (auto const [_, inserted]{ r->product_dependencies.emplace(
              product_name,
              recipe::product_dependency{ .name = product_name,
                                          .needed_by = needed_by_phase,
                                          .provider = nullptr,
                                          .constraint_identity = dep_spec->identity }) };
          !inserted) {
        throw std::runtime_error("Duplicate product dependency '" + product_name +
                                 "' in recipe '" + r->spec->identity + "'");
      }
    }

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
      // Strong product dependency (has source) - wire directly, no weak resolution needed
      recipe *dep{ eng.ensure_recipe(dep_spec) };

      r->dependencies[dep_spec->identity] = { dep, needed_by_phase };
      ENVY_TRACE_DEPENDENCY_ADDED(r->spec->identity, dep_spec->identity, needed_by_phase);
      auto &pd{ r->product_dependencies.at(*dep_spec->product) };
      pd.provider = dep;
      pd.constraint_identity = dep_spec->identity;

      std::vector<std::string> child_chain{ ctx.ancestor_chain };
      child_chain.push_back(r->spec->identity);
      eng.start_recipe_thread(dep, recipe_phase::recipe_fetch, std::move(child_chain));

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
}

}  // namespace

void run_recipe_fetch_phase(recipe *r, engine &eng) {
  recipe_spec const &spec{ *r->spec };
  phase_trace_scope const phase_scope{ spec.identity,
                                       recipe_phase::recipe_fetch,
                                       std::chrono::steady_clock::now() };

  // Fetch recipe based on source type
  std::filesystem::path const recipe_path{ [&] {
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

  // Load and validate recipe script
  auto lua{ create_lua_state() };
  load_recipe_script(*lua, recipe_path, spec.identity);

  // Store recipe file path for error reporting
  r->recipe_file_path = recipe_path;

  std::string const declared_identity{ [&] {
    try {
      sol::object identity_obj{ (*lua)["IDENTITY"] };
      if (!identity_obj.valid() || identity_obj.get_type() != sol::type::string) {
        throw std::runtime_error("Recipe must define 'IDENTITY' global as a string");
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

  // Determine recipe type (user-managed or cache-managed)
  {
    sol::state_view lua_view{ *lua };
    bool const has_check{ lua_view["CHECK"].is<sol::protected_function>() ||
                          lua_view["CHECK"].is<std::string>() };
    bool const has_install{ lua_view["INSTALL"].is<sol::protected_function>() ||
                            lua_view["INSTALL"].is<std::string>() };

    if (has_check) {
      if (!has_install) {
        throw std::runtime_error("User-managed recipe must define 'install' function: " +
                                 spec.identity);
      }
      r->type = recipe_type::USER_MANAGED;
    } else {
      r->type = recipe_type::CACHE_MANAGED;
    }
  }

  r->products = parse_products_table(spec, *lua, r);
  for (auto const &[name, value] : r->products) {
    ENVY_TRACE_EMIT((trace_events::product_parsed{
        .recipe = spec.identity, .product_name = name, .product_value = value}));
  }
  r->owned_dependency_specs = parse_dependencies_table(*lua, recipe_path, spec);

  for (auto *dep_spec : r->owned_dependency_specs) { dep_spec->parent = r->spec; }

  try {  // Store options in Lua registry
    lua->registry()[ENVY_OPTIONS_RIDX] =
        store_options_in_registry(*lua, spec.serialized_options);
  } catch (std::runtime_error const &e) {
    throw std::runtime_error(e.what() + std::string(" for ") + spec.identity);
  }

  run_validate(r, *lua);

  // Extract dependency identities for ctx.asset() validation
  r->declared_dependencies.reserve(r->owned_dependency_specs.size());
  for (auto const *dep_spec : r->owned_dependency_specs) {
    r->declared_dependencies.push_back(dep_spec->identity);
  }

  r->lua = std::move(lua);

  wire_dependency_graph(r, eng);
}

}  // namespace envy

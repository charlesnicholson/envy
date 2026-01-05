#include "phase_spec_fetch.h"

#include "bundle.h"
#include "engine.h"
#include "extract.h"
#include "fetch.h"
#include "lua_ctx/lua_phase_context.h"
#include "lua_envy.h"
#include "lua_error_formatter.h"
#include "pkg.h"
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
    throw std::runtime_error("Spec must define 'FETCH' or both 'CHECK' and 'INSTALL': " +
                             identity);
  }
}

sol_state_ptr create_lua_state() {
  auto lua{ sol_util_make_lua_state() };
  lua_envy_install(*lua);
  return lua;
}

void load_spec_script(sol::state &lua,
                      std::filesystem::path const &spec_path,
                      std::string const &identity) {
  sol::protected_function_result result{ lua.safe_script_file(spec_path.string(),
                                                              sol::script_pass_on_error) };
  if (!result.valid()) {
    sol::error err = result;
    throw std::runtime_error("Failed to load spec: " + identity + ": " + err.what());
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
          "Spec " + identity +
          " has CHECK verb (user-managed) but declares FETCH phase. "
          "User-managed packages cannot use cache-managed phases (FETCH/STAGE/BUILD). "
          "Remove CHECK verb or remove FETCH phase.");
    }

    sol::object stage_obj{ lua["STAGE"] };
    if (stage_obj.valid() && stage_obj.is<sol::protected_function>()) {
      throw std::runtime_error(
          "Spec " + identity +
          " has CHECK verb (user-managed) but declares STAGE phase. "
          "User-managed packages cannot use cache-managed phases (FETCH/STAGE/BUILD). "
          "Remove CHECK verb or remove STAGE phase.");
    }

    sol::object build_obj{ lua["BUILD"] };
    if (build_obj.valid() && build_obj.is<sol::protected_function>()) {
      throw std::runtime_error(
          "Spec " + identity +
          " has CHECK verb (user-managed) but declares BUILD phase. "
          "User-managed packages cannot use cache-managed phases (FETCH/STAGE/BUILD). "
          "Remove CHECK verb or remove BUILD phase.");
    }
  }
}

std::filesystem::path get_cached_spec_path(pkg const *p) {
  return p->cache_ptr->ensure_spec(p->cfg->identity).pkg_path / "spec.lua";
}

std::filesystem::path fetch_local_source(pkg_cfg const &cfg) {
  auto const *local_src{ std::get_if<pkg_cfg::local_source>(&cfg.source) };
  return local_src->file_path;
}

std::filesystem::path fetch_remote_source(pkg_cfg const &cfg, pkg *p) {
  auto const *remote_src{ std::get_if<pkg_cfg::remote_source>(&cfg.source) };
  auto cache_result{ p->cache_ptr->ensure_spec(cfg.identity) };

  if (cache_result.lock) {
    tui::debug("fetch spec %s from %s", cfg.identity.c_str(), remote_src->url.c_str());
    std::filesystem::path fetch_dest{ cache_result.lock->install_dir() / "spec.lua" };

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
        throw std::runtime_error("Unsupported URL scheme for spec fetch: " +
                                 remote_src->url);
    }

    auto const results{ fetch({ req }) };
    if (results.empty() || std::holds_alternative<std::string>(results[0])) {
      throw std::runtime_error(
          "Failed to fetch spec: " +
          (results.empty() ? "no results" : std::get<std::string>(results[0])));
    }

    if (!remote_src->sha256.empty()) {
      tui::debug("verifying SHA256 for spec %s", cfg.identity.c_str());
      sha256_verify(remote_src->sha256, sha256(fetch_dest));
    }

    cache_result.lock->mark_install_complete();
    cache_result.lock.reset();
  }

  return cache_result.pkg_path / "spec.lua";
}

std::filesystem::path fetch_git_source(pkg_cfg const &cfg, pkg *p) {
  auto const *git_src{ std::get_if<pkg_cfg::git_source>(&cfg.source) };
  auto cache_result{ p->cache_ptr->ensure_spec(cfg.identity) };

  if (cache_result.lock) {
    tui::debug("fetch spec %s from git %s @ %s",
               cfg.identity.c_str(),
               git_src->url.c_str(),
               git_src->ref.c_str());

    std::filesystem::path install_dir{ cache_result.lock->install_dir() };
    auto const results{ fetch({ fetch_request_git{ .source = git_src->url,
                                                   .destination = install_dir,
                                                   .ref = git_src->ref } }) };
    if (results.empty() || std::holds_alternative<std::string>(results[0])) {
      throw std::runtime_error(
          "Failed to fetch git spec: " +
          (results.empty() ? "no results" : std::get<std::string>(results[0])));
    }

    cache_result.lock->mark_install_complete();
    cache_result.lock.reset();
  }

  return cache_result.pkg_path / "spec.lua";
}

std::filesystem::path fetch_bundle_and_resolve_spec(pkg_cfg const &cfg,
                                                    pkg *p,
                                                    engine &eng) {
  auto const *bundle_src{ std::get_if<pkg_cfg::bundle_source>(&cfg.source) };
  std::string const &bundle_id{ bundle_src->bundle_identity };

  // Check if bundle is already registered
  if (bundle * existing{ eng.find_bundle(bundle_id) }) {
    std::filesystem::path spec_path{ existing->resolve_spec_path(cfg.identity) };
    if (spec_path.empty()) {
      throw std::runtime_error("Spec '" + cfg.identity + "' not found in bundle '" +
                               bundle_id + "'");
    }
    return spec_path;
  }

  // Bundle needs to be fetched - use bundle cache directory
  auto cache_result{ p->cache_ptr->ensure_spec(bundle_id) };

  if (cache_result.lock) {
    tui::debug("fetch bundle %s for spec %s", bundle_id.c_str(), cfg.identity.c_str());
    std::filesystem::path const install_dir{ cache_result.lock->install_dir() };

    // Fetch based on underlying source type
    std::visit(
        match{
            [&](pkg_cfg::remote_source const &remote) {  // remote file
              std::filesystem::path fetch_dest{ cache_result.lock->fetch_dir() /
                                                uri_extract_filename(remote.url) };

              auto const info{ uri_classify(remote.url) };
              fetch_request req;
              switch (info.scheme) {
                case uri_scheme::HTTP:
                  req = fetch_request_http{ .source = remote.url,
                                            .destination = fetch_dest };
                  break;
                case uri_scheme::HTTPS:
                  req = fetch_request_https{ .source = remote.url,
                                             .destination = fetch_dest };
                  break;
                case uri_scheme::FTP:
                  req =
                      fetch_request_ftp{ .source = remote.url, .destination = fetch_dest };
                  break;
                case uri_scheme::FTPS:
                  req = fetch_request_ftps{ .source = remote.url,
                                            .destination = fetch_dest };
                  break;
                case uri_scheme::S3:
                  req =
                      fetch_request_s3{ .source = remote.url, .destination = fetch_dest };
                  break;
                default:
                  throw std::runtime_error("Unsupported URL scheme for bundle fetch: " +
                                           remote.url);
              }

              auto const results{ fetch({ req }) };
              if (results.empty() || std::holds_alternative<std::string>(results[0])) {
                throw std::runtime_error(
                    "Failed to fetch bundle: " +
                    (results.empty() ? "no results" : std::get<std::string>(results[0])));
              }

              if (!remote.sha256.empty()) {
                sha256_verify(remote.sha256, sha256(fetch_dest));
              }

              // Extract bundle archive into install_dir
              extract(fetch_dest, install_dir);
            },

            [&](pkg_cfg::local_source const &local) {  // local source
              // Local source - copy directory or extract archive
              if (std::filesystem::is_directory(local.file_path)) {
                std::filesystem::copy(
                    local.file_path,
                    install_dir,
                    std::filesystem::copy_options::recursive |
                        std::filesystem::copy_options::overwrite_existing);
              } else {
                extract(local.file_path, install_dir);
              }
            },

            [&](pkg_cfg::git_source const &git) {  // git source
              auto const results{ fetch({ fetch_request_git{ .source = git.url,
                                                             .destination = install_dir,
                                                             .ref = git.ref } }) };
              if (results.empty() || std::holds_alternative<std::string>(results[0])) {
                throw std::runtime_error(
                    "Failed to fetch git bundle: " +
                    (results.empty() ? "no results" : std::get<std::string>(results[0])));
              }
            },
        },
        bundle_src->fetch_source);

    cache_result.lock->mark_install_complete();
    cache_result.lock.reset();
  }

  // Parse bundle manifest and validate
  bundle parsed{ bundle::from_path(cache_result.pkg_path) };

  if (parsed.identity != bundle_id) {
    throw std::runtime_error("Bundle identity mismatch: expected '" + bundle_id +
                             "' but manifest declares '" + parsed.identity + "'");
  }

  parsed.validate_integrity();

  // Register the bundle
  bundle *b{
    eng.register_bundle(bundle_id, std::move(parsed.specs), cache_result.pkg_path)
  };

  // Resolve spec path within bundle
  std::filesystem::path spec_path{ b->resolve_spec_path(cfg.identity) };
  if (spec_path.empty()) {
    throw std::runtime_error("Spec '" + cfg.identity + "' not found in bundle '" +
                             bundle_id + "'");
  }

  return spec_path;
}

std::filesystem::path fetch_custom_function(pkg_cfg const &cfg, pkg *p, engine &eng) {
  if (!cfg.parent) {
    throw std::runtime_error("Custom fetch function spec has no parent: " + cfg.identity);
  }

  pkg *parent{ eng.find_exact(pkg_key(*cfg.parent)) };
  if (!parent) {
    throw std::runtime_error("Custom fetch function spec parent not found: " +
                             cfg.identity);
  }

  auto cache_result{ p->cache_ptr->ensure_spec(cfg.identity) };

  if (cache_result.lock) {
    tui::debug("fetch spec %s via custom fetch function", cfg.identity.c_str());

    // Set up paths for custom fetch function
    std::filesystem::path const tmp_dir{ cache_result.lock->work_dir() / "tmp" };
    std::filesystem::create_directories(tmp_dir);

    std::lock_guard const lock(parent->lua_mutex);

    if (!parent->lua) {
      throw std::runtime_error("Custom fetch function spec has no parent Lua state: " +
                               cfg.identity);
    }

    {
      sol::state_view parent_lua_view{ *parent->lua };

      // Look up the fetch function from parent's dependencies
      if (!pkg_cfg::lookup_and_push_source_fetch(parent_lua_view, cfg.identity)) {
        throw std::runtime_error("Failed to lookup fetch function for: " + cfg.identity);
      }

      // Stack: [function]
      sol::protected_function fetch_func{ parent_lua_view,
                                          sol::stack_reference(parent_lua_view.lua_state(),
                                                               -1) };
      lua_pop(parent_lua_view.lua_state(), 1);  // pop function
      sol::object options_obj{ parent_lua_view.registry()[ENVY_OPTIONS_RIDX] };

      // Set up phase context with lock so envy.commit_fetch can access paths.
      // The inline source.fetch runs in parent's Lua state, so we pass the lock
      // explicitly rather than through parent->lock.
      phase_context_guard ctx_guard{ &eng, parent, tmp_dir, cache_result.lock.get() };

      sol::protected_function_result fetch_result{ fetch_func(tmp_dir.string(),
                                                              options_obj) };

      if (!fetch_result.valid()) {
        sol::error err = fetch_result;
        throw std::runtime_error("Fetch function failed for " + cfg.identity + ": " +
                                 err.what());
      }
    }

    // Custom fetch creates spec.lua in fetch_dir via envy.commit_fetch.
    // The lock destructor will clean up fetch_dir, so move spec.lua to install_dir
    // which gets renamed to pkg_dir on successful completion.
    std::filesystem::path const fetch_dir{ cache_result.lock->fetch_dir() };
    std::filesystem::path const install_dir{ cache_result.lock->install_dir() };
    std::filesystem::path const spec_src{ fetch_dir / "spec.lua" };
    std::filesystem::path const spec_dst{ install_dir / "spec.lua" };

    if (!std::filesystem::exists(spec_src)) {
      throw std::runtime_error("Custom fetch did not create spec.lua for: " +
                               cfg.identity);
    }

    std::filesystem::rename(spec_src, spec_dst);

    cache_result.lock->mark_install_complete();
    cache_result.lock.reset();

    // Spec is now at pkg_path / "spec.lua" after lock cleanup
    return cache_result.pkg_path / "spec.lua";
  }

  // Cache was already complete - spec.lua should exist in pkg_path
  std::filesystem::path const spec_path{ cache_result.pkg_path / "spec.lua" };
  if (!std::filesystem::exists(spec_path)) {
    throw std::runtime_error("Custom fetch did not create spec.lua for: " + cfg.identity);
  }

  return spec_path;
}

std::unordered_map<std::string, std::string> parse_products_table(pkg_cfg const &cfg,
                                                                  sol::state &lua,
                                                                  pkg *p) {
  std::unordered_map<std::string, std::string> parsed_products;
  sol::object products_obj{ lua["PRODUCTS"] };
  std::string const &id{ cfg.identity };

  if (!products_obj.valid()) { return parsed_products; }

  sol::table products_table;

  // Handle programmatic products: function that takes options, returns table
  if (products_obj.get_type() == sol::type::function) {
    sol::function products_fn{ products_obj.as<sol::function>() };

    // Deserialize options from cfg to pass to products function
    std::string const opts_str{ "return " + cfg.serialized_options };
    auto opts_result{ lua.safe_script(opts_str, sol::script_pass_on_error) };
    if (!opts_result.valid()) {
      sol::error err = opts_result;
      throw std::runtime_error("Failed to deserialize options for PRODUCTS function: " +
                               std::string(err.what()));
    }
    sol::object options{ opts_result.get<sol::object>() };

    // Call products(options) with enriched error handling
    sol::protected_function_result result{ call_lua_function_with_enriched_errors(
        p,
        "PRODUCTS",
        [&]() { return sol::protected_function_result{ products_fn(options) }; }) };

    sol::object result_obj{ result };
    if (result_obj.get_type() != sol::type::table) {
      throw std::runtime_error("PRODUCTS function must return table in spec '" + id + "'");
    }
    products_table = result_obj.as<sol::table>();
  } else if (products_obj.get_type() == sol::type::table) {
    products_table = products_obj.as<sol::table>();
  } else {
    throw std::runtime_error("PRODUCTS must be table or function in spec '" + id + "'");
  }
  sol::object check_obj{ lua["CHECK"] };
  bool const has_check{ check_obj.valid() && check_obj.get_type() == sol::type::function };

  for (auto const &[key, value] : products_table) {
    sol::object key_obj(key);
    sol::object val_obj(value);

    if (!key_obj.is<std::string>()) {
      throw std::runtime_error("PRODUCTS key must be string in spec '" + id + "'");
    }
    if (!val_obj.is<std::string>()) {
      throw std::runtime_error("PRODUCTS value must be string in spec '" + id + "'");
    }

    std::string key_str{ key_obj.as<std::string>() };
    std::string val_str{ val_obj.as<std::string>() };

    if (key_str.empty()) {
      throw std::runtime_error("PRODUCTS key cannot be empty in spec '" + id + "'");
    }

    for (char c : key_str) {
      bool const dangerous{ c < 0x21 || c > 0x7e || c == '"' || c == '\'' || c == '$' ||
                            c == '`' || c == '%' || c == '\\' || c == '!' };
      if (dangerous) {
        throw std::runtime_error("PRODUCTS key '" + key_str +
                                 "' contains shell-unsafe character in spec '" + id + "'");
      }
    }

    if (val_str.empty()) {
      throw std::runtime_error("PRODUCTS value cannot be empty in spec '" + id + "'");
    }

    if (!has_check) {  // Validate path safety for cached packages
      std::filesystem::path product_path{ val_str };

      if (product_path.is_absolute() || (!val_str.empty() && val_str[0] == '/')) {
        throw std::runtime_error("PRODUCTS value '" + val_str +
                                 "' cannot be absolute path in spec '" + id + "'");
      }

      for (auto const &component : product_path) {  // Check for path traversal components
        if (component == "..") {
          throw std::runtime_error("PRODUCTS value '" + val_str +
                                   "' cannot contain path traversal (..) in spec '" + id +
                                   "'");
        }
      }
    }

    parsed_products[std::move(key_str)] = std::move(val_str);
  }

  return parsed_products;
}

std::vector<pkg_cfg *> parse_dependencies_table(sol::state &lua,
                                                std::filesystem::path const &spec_path,
                                                pkg_cfg const &cfg) {
  std::vector<pkg_cfg *> parsed_deps;
  sol::object deps_obj{ lua["DEPENDENCIES"] };

  if (!deps_obj.valid() || deps_obj.get_type() != sol::type::table) { return parsed_deps; }

  sol::table deps_table{ deps_obj.as<sol::table>() };
  for (size_t i{ 1 }; i <= deps_table.size(); ++i) {
    pkg_cfg *dep_cfg{ pkg_cfg::parse(deps_table[i], spec_path, true) };

    if (!cfg.identity.starts_with("local.") && dep_cfg->identity.starts_with("local.")) {
      throw std::runtime_error("non-local spec '" + cfg.identity +
                               "' cannot depend on local spec '" + dep_cfg->identity +
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

void run_validate(pkg *p, sol::state &lua) {
  sol::table globals{ lua.globals() };
  std::optional<sol::protected_function> validate_fn;
  try {
    validate_fn =
        sol_util_get_optional<sol::protected_function>(globals, "VALIDATE", "Spec");
  } catch (std::runtime_error const &e) {
    throw std::runtime_error(std::string(e.what()) + " in spec '" + p->cfg->identity +
                             "'");
  }
  if (!validate_fn.has_value()) { return; }

  sol::object options_obj{ lua.registry()[ENVY_OPTIONS_RIDX] };

  sol::protected_function_result result{ call_lua_function_with_enriched_errors(
      p,
      "validate",
      [&]() { return (*validate_fn)(options_obj); }) };

  sol::object ret_obj{ result };
  sol::type const ret_type{ ret_obj.get_type() };

  auto const failure_prefix{ [&]() {
    return "VALIDATE failed for " + p->cfg->format_key();
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
                               p->cfg->format_key());
  }
}

void wire_dependency_graph(pkg *p, engine &eng) {
  auto &ctx{ eng.get_execution_ctx(p) };

  for (auto *dep_cfg : p->owned_dependency_cfgs) {
    engine_validate_dependency_cycle(dep_cfg->identity,
                                     ctx.ancestor_chain,
                                     p->cfg->identity,
                                     "Dependency");

    pkg_phase const needed_by_phase{ dep_cfg->needed_by.has_value()
                                         ? static_cast<pkg_phase>(*dep_cfg->needed_by)
                                         : pkg_phase::pkg_build };
    bool const is_product_dep{ dep_cfg->product.has_value() };

    if (is_product_dep) {
      std::string const &product_name{ *dep_cfg->product };

      if (auto const [_, inserted]{ p->product_dependencies.emplace(
              product_name,
              pkg::product_dependency{ .name = product_name,
                                       .needed_by = needed_by_phase,
                                       .provider = nullptr,
                                       .constraint_identity = dep_cfg->identity }) };
          !inserted) {
        throw std::runtime_error("Duplicate product dependency '" + product_name +
                                 "' in spec '" + p->cfg->identity + "'");
      }
    }

    if (dep_cfg->is_weak_reference()) {
      p->weak_references.push_back(pkg::weak_reference{
          .query = is_product_dep ? *dep_cfg->product : dep_cfg->identity,
          .fallback = dep_cfg->weak,
          .needed_by = needed_by_phase,
          .resolved = nullptr,
          .is_product = is_product_dep,
          .constraint_identity = is_product_dep ? dep_cfg->identity : "" });
      continue;
    }

    if (is_product_dep) {
      // Strong product dependency (has source) - wire directly, no weak resolution needed
      pkg *dep{ eng.ensure_pkg(dep_cfg) };

      p->dependencies[dep_cfg->identity] = { dep, needed_by_phase };
      ENVY_TRACE_DEPENDENCY_ADDED(p->cfg->identity, dep_cfg->identity, needed_by_phase);
      auto &pd{ p->product_dependencies.at(*dep_cfg->product) };
      pd.provider = dep;
      pd.constraint_identity = dep_cfg->identity;

      std::vector<std::string> child_chain{ ctx.ancestor_chain };
      child_chain.push_back(p->cfg->identity);
      eng.start_pkg_thread(dep, pkg_phase::spec_fetch, std::move(child_chain));

      continue;
    }

    pkg *dep{ eng.ensure_pkg(dep_cfg) };

    // Store dependency info in parent's map for ctx.pkg() lookup and phase coordination
    p->dependencies[dep_cfg->identity] = { dep, needed_by_phase };
    ENVY_TRACE_DEPENDENCY_ADDED(p->cfg->identity, dep_cfg->identity, needed_by_phase);

    std::vector<std::string> child_chain{ ctx.ancestor_chain };
    child_chain.push_back(p->cfg->identity);
    eng.start_pkg_thread(dep, pkg_phase::spec_fetch, std::move(child_chain));
  }
}

}  // namespace

void run_spec_fetch_phase(pkg *p, engine &eng) {
  pkg_cfg const &cfg{ *p->cfg };
  phase_trace_scope const phase_scope{ cfg.identity,
                                       pkg_phase::spec_fetch,
                                       std::chrono::steady_clock::now() };

  // Fetch spec based on source type
  std::filesystem::path const spec_path{ [&] {
    if (auto const *local_src{ std::get_if<pkg_cfg::local_source>(&cfg.source) }) {
      return fetch_local_source(cfg);
    } else if (std::holds_alternative<pkg_cfg::remote_source>(cfg.source)) {
      return fetch_remote_source(cfg, p);
    } else if (std::holds_alternative<pkg_cfg::git_source>(cfg.source)) {
      return fetch_git_source(cfg, p);
    } else if (cfg.has_fetch_function()) {
      return fetch_custom_function(cfg, p, eng);
    } else if (std::holds_alternative<pkg_cfg::bundle_source>(cfg.source)) {
      return fetch_bundle_and_resolve_spec(cfg, p, eng);
    } else {
      throw std::runtime_error("Unsupported source type: " + cfg.identity);
    }
  }() };

  if (!std::filesystem::exists(spec_path)) {
    throw std::runtime_error("Spec source not found: " + spec_path.string() +
                             " (for spec '" + cfg.identity + "')");
  }

  // Load and validate spec script
  auto lua{ create_lua_state() };
  load_spec_script(*lua, spec_path, cfg.identity);

  // Store spec file path for error reporting
  p->spec_file_path = spec_path;

  std::string const declared_identity{ [&] {
    try {
      sol::object identity_obj{ (*lua)["IDENTITY"] };
      if (!identity_obj.valid() || identity_obj.get_type() != sol::type::string) {
        throw std::runtime_error("Spec must define 'IDENTITY' global as a string");
      }
      return identity_obj.as<std::string>();
    } catch (std::runtime_error const &e) {
      throw std::runtime_error(std::string(e.what()) + " (in spec: " + cfg.identity + ")");
    }
  }() };

  if (declared_identity != cfg.identity) {
    throw std::runtime_error("Identity mismatch: expected '" + cfg.identity +
                             "' but spec declares '" + declared_identity + "'");
  }

  validate_phases(sol::state_view{ *lua }, cfg.identity);

  // Determine package type (user-managed or cache-managed)
  {
    sol::state_view lua_view{ *lua };
    bool const has_check{ lua_view["CHECK"].is<sol::protected_function>() ||
                          lua_view["CHECK"].is<std::string>() };
    bool const has_install{ lua_view["INSTALL"].is<sol::protected_function>() ||
                            lua_view["INSTALL"].is<std::string>() };

    if (has_check) {
      if (!has_install) {
        throw std::runtime_error("User-managed spec must define 'install' function: " +
                                 cfg.identity);
      }
      p->type = pkg_type::USER_MANAGED;
    } else {
      p->type = pkg_type::CACHE_MANAGED;
    }
  }

  p->products = parse_products_table(cfg, *lua, p);
  for (auto const &[name, value] : p->products) {
    ENVY_TRACE_EMIT((trace_events::product_parsed{ .spec = cfg.identity,
                                                   .product_name = name,
                                                   .product_value = value }));
  }
  p->owned_dependency_cfgs = parse_dependencies_table(*lua, spec_path, cfg);

  for (auto *dep_cfg : p->owned_dependency_cfgs) { dep_cfg->parent = p->cfg; }

  try {  // Store options in Lua registry
    lua->registry()[ENVY_OPTIONS_RIDX] =
        store_options_in_registry(*lua, cfg.serialized_options);
  } catch (std::runtime_error const &e) {
    throw std::runtime_error(e.what() + std::string(" for ") + cfg.identity);
  }

  run_validate(p, *lua);

  // Extract dependency identities for ctx.pkg() validation
  p->declared_dependencies.reserve(p->owned_dependency_cfgs.size());
  for (auto const *dep_cfg : p->owned_dependency_cfgs) {
    p->declared_dependencies.push_back(dep_cfg->identity);
  }

  p->lua = std::move(lua);

  wire_dependency_graph(p, eng);
}

}  // namespace envy

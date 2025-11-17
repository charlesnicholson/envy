#include "phase_recipe_fetch.h"

#include "engine.h"
#include "fetch.h"
#include "recipe.h"
#include "sha256.h"
#include "tui.h"

#include <stdexcept>
#include <vector>

namespace envy {

namespace {

void validate_phases(lua_State *lua, std::string const &identity) {
  lua_getglobal(lua, "fetch");
  int const fetch_type{ lua_type(lua, -1) };
  bool const has_fetch{ fetch_type == LUA_TFUNCTION || fetch_type == LUA_TSTRING ||
                        fetch_type == LUA_TTABLE };
  lua_pop(lua, 1);

  if (has_fetch) { return; }

  lua_getglobal(lua, "check");
  bool const has_check{ lua_isfunction(lua, -1) };
  lua_pop(lua, 1);

  lua_getglobal(lua, "install");
  bool const has_install{ lua_isfunction(lua, -1) };
  lua_pop(lua, 1);

  if (!has_check || !has_install) {
    throw std::runtime_error("Recipe must define 'fetch' or both 'check' and 'install': " +
                             identity);
  }
}

}  // namespace

void run_recipe_fetch_phase(recipe *r,
                            engine &eng,
                            std::unordered_set<std::string> const &ancestors) {
  recipe_spec const &spec = r->spec;
  std::string const key{ spec.format_key() };
  tui::trace("phase recipe_fetch START [%s]", key.c_str());

  auto lua_state{ lua_make() };
  lua_add_envy(lua_state);

  std::filesystem::path recipe_path;
  if (auto const *local_src{ std::get_if<recipe_spec::local_source>(&spec.source) }) {
    recipe_path = local_src->file_path;
    if (!lua_run_file(lua_state, recipe_path)) {
      throw std::runtime_error("Failed to load recipe: " + spec.identity);
    }
  } else if (auto const *remote_src{
                 std::get_if<recipe_spec::remote_source>(&spec.source) }) {
    auto cache_result{ r->cache_ptr->ensure_recipe(spec.identity) };

    if (cache_result.lock) {
      tui::trace("fetch recipe %s from %s",
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
        tui::trace("verifying SHA256 for recipe %s", spec.identity.c_str());
        sha256_verify(remote_src->sha256, sha256(fetch_dest));
      }

      cache_result.lock->mark_install_complete();
      cache_result.lock.reset();
    }

    recipe_path = cache_result.asset_path / "recipe.lua";
    if (!lua_run_file(lua_state, recipe_path)) {
      throw std::runtime_error("Failed to load recipe: " + spec.identity);
    }
  } else if (auto const *git_src{ std::get_if<recipe_spec::git_source>(&spec.source) }) {
    auto cache_result{ r->cache_ptr->ensure_recipe(spec.identity) };

    if (cache_result.lock) {
      tui::trace("fetch recipe %s from git %s @ %s",
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
    if (!lua_run_file(lua_state, recipe_path)) {
      throw std::runtime_error("Failed to load recipe: " + spec.identity);
    }
  } else {
    throw std::runtime_error("Unsupported source type: " + spec.identity);
  }

  std::string const declared_identity{ [&] {
    try {
      return lua_global_to_string(lua_state.get(), "identity");
    } catch (std::runtime_error const &e) {
      throw std::runtime_error(std::string(e.what()) + " (in recipe: " + spec.identity +
                               ")");
    }
  }() };

  if (declared_identity != spec.identity) {
    throw std::runtime_error("Identity mismatch: expected '" + spec.identity +
                             "' but recipe declares '" + declared_identity + "'");
  }

  validate_phases(lua_state.get(), spec.identity);

  std::vector<recipe_spec> dep_configs;
  if (auto const deps_array{ lua_global_to_array(lua_state.get(), "dependencies") }) {
    for (auto const &dep_val : *deps_array) {
      auto dep_cfg{ recipe_spec::parse(dep_val, recipe_path) };

      if (!spec.identity.starts_with("local.") && dep_cfg.identity.starts_with("local.")) {
        throw std::runtime_error("Security violation: non-local recipe '" + spec.identity +
                                 "' cannot depend on local recipe '" + dep_cfg.identity +
                                 "'");
      }

      dep_configs.push_back(dep_cfg);
    }
  }

  // Extract dependency identities for validation
  std::vector<std::string> const dep_identities{ [&]() {
    std::vector<std::string> result;
    result.reserve(dep_configs.size());
    for (auto const &dep_cfg : dep_configs) { result.push_back(dep_cfg.identity); }
    return result;
  }() };

  // Store lua_state and declared dependencies in recipe
  r->lua_state = std::move(lua_state);
  r->declared_dependencies = std::move(dep_identities);

  // Build dependency graph: create child graphs, wire edges, start them
  std::unordered_set<std::string> dep_ancestors{ ancestors };
  dep_ancestors.insert(spec.identity);

  for (auto const &dep_cfg : dep_configs) {
    // Create dependency recipe via engine factory
    recipe *dep{ eng.ensure_recipe(dep_cfg) };

    // Store dependency in parent's map for ctx.asset() lookup
    r->dependencies[dep_cfg.identity] = dep;

    // Note: In the new architecture, the engine manages recipe threads and phase
    // progression. Dependencies are started by engine::resolve_graph(), and the
    // needed_by field will be used during phase coordination via
    // engine::ensure_recipe_at_phase().
  }
}

}  // namespace envy

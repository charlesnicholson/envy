#include "phase_recipe_fetch.h"

#include "create_recipe_nodes.h"
#include "fetch.h"
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

void run_recipe_fetch_phase(recipe_spec const &spec,
                            std::string const &key,
                            graph_state &state,
                            std::unordered_set<std::string> const &ancestors) {
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
    auto cache_result{ state.cache_.ensure_recipe(spec.identity) };

    if (cache_result.lock) {
      tui::trace("fetch recipe %s from %s",
                 spec.identity.c_str(),
                 remote_src->url.c_str());
      std::filesystem::path fetch_dest{ cache_result.lock->install_dir() / "recipe.lua" };

      auto const results{ fetch(
          { { .source = remote_src->url, .destination = fetch_dest } }) };
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
  } else {
    throw std::runtime_error("Only local and remote sources supported: " + spec.identity);
  }

  std::string const declared_identity = [&] {
    try {
      return lua_global_to_string(lua_state.get(), "identity");
    } catch (std::runtime_error const &e) {
      throw std::runtime_error(std::string(e.what()) + " (in recipe: " + spec.identity +
                               ")");
    }
  }();

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
  std::vector<std::string> dep_identities;
  dep_identities.reserve(dep_configs.size());
  for (auto const &dep_cfg : dep_configs) { dep_identities.push_back(dep_cfg.identity); }

  {
    typename decltype(state.recipes)::accessor acc;
    if (state.recipes.find(acc, key)) {
      acc->second.lua_state = std::move(lua_state);
      acc->second.declared_dependencies = std::move(dep_identities);
    }
  }

  std::unordered_set<std::string> dep_ancestors{ ancestors };
  dep_ancestors.insert(key);

  for (auto const &dep_cfg : dep_configs) {
    auto const dep_key{ make_canonical_key(dep_cfg.identity, dep_cfg.options) };

    create_recipe_nodes(dep_key, dep_cfg, state, dep_ancestors);

    {
      typename decltype(state.recipes)::const_accessor dep_acc, parent_acc;
      if (state.recipes.find(dep_acc, dep_key) && state.recipes.find(parent_acc, key)) {
        tbb::flow::make_edge(*dep_acc->second.completion_node,
                             *parent_acc->second.check_node);

        if (dep_acc->second.completed) {
          parent_acc->second.check_node->try_put(tbb::flow::continue_msg{});
        }
      }
    }

    auto [iter, inserted]{ state.triggered.insert(dep_key) };
    if (inserted) {
      typename decltype(state.recipes)::const_accessor acc;
      if (state.recipes.find(acc, dep_key)) {
        acc->second.recipe_fetch_node->try_put(tbb::flow::continue_msg{});
      }
    }
  }
}

}  // namespace envy

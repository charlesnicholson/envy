#include "recipe.h"

#include "cache.h"
#include "lua_util.h"
#include "tui.h"
#include "uri.h"

#include "tbb/concurrent_hash_map.h"
#include "tbb/task_group.h"

#include <algorithm>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace envy {

struct recipe::impl {
  cfg cfg_;
  lua_state_ptr lua_state_;
  std::vector<recipe *> dependencies_;
};

namespace {

thread_local std::vector<std::string> resolution_stack;

bool parse_identity(std::string const &identity,
                    std::string &out_namespace,
                    std::string &out_name,
                    std::string &out_version) {
  auto const at_pos{ identity.find('@') };
  if (at_pos == std::string::npos || at_pos == 0 || at_pos == identity.size() - 1) {
    return false;
  }

  auto const dot_pos{ identity.find('.') };
  if (dot_pos == std::string::npos || dot_pos == 0 || dot_pos >= at_pos) { return false; }

  out_namespace = identity.substr(0, dot_pos);
  out_name = identity.substr(dot_pos + 1, at_pos - dot_pos - 1);
  out_version = identity.substr(at_pos + 1);

  return !out_namespace.empty() && !out_name.empty() && !out_version.empty();
}

}  // namespace

recipe::cfg recipe::cfg::parse(lua_value const &lua_val,
                               std::filesystem::path const &base_path) {
  cfg result;

  //  "namespace.name@version" shorthand requires url or file
  if (auto const *str{ lua_val.get<std::string>() }) {
    throw std::runtime_error(
        "Recipe shorthand string syntax requires table with 'url' or 'file': " + *str);
  }

  auto const *table{ lua_val.get<lua_table>() };
  if (!table) { throw std::runtime_error("Recipe entry must be string or table"); }

  result.identity = [&] {
    auto const recipe_it{ table->find("recipe") };
    if (recipe_it == table->end()) {
      throw std::runtime_error("Recipe table missing required 'recipe' field");
    }
    if (auto const *recipe_str{ recipe_it->second.get<std::string>() }) {
      return *recipe_str;
    }
    throw std::runtime_error("Recipe 'recipe' field must be string");
  }();

  std::string ns, name, ver;
  if (!parse_identity(result.identity, ns, name, ver)) {
    throw std::runtime_error("Invalid recipe identity format: " + result.identity);
  }

  auto const url_it{ table->find("url") };
  auto const file_it{ table->find("file") };

  if (url_it != table->end() && file_it != table->end()) {
    throw std::runtime_error("Recipe cannot specify both 'url' and 'file'");
  }

  if (url_it != table->end()) {  // Remote source
    if (auto const *url{ url_it->second.get<std::string>() }) {
      auto const sha256_it{ table->find("sha256") };
      if (sha256_it == table->end()) {
        throw std::runtime_error("Recipe with 'url' must specify 'sha256'");
      }
      if (auto const *sha256{ sha256_it->second.get<std::string>() }) {
        result.source = remote_source{ .url = *url, .sha256 = *sha256 };
      } else {
        throw std::runtime_error("Recipe 'sha256' field must be string");
      }
    } else {
      throw std::runtime_error("Recipe 'url' field must be string");
    }
  } else if (file_it != table->end()) {  // Local source
    if (auto const *file{ file_it->second.get<std::string>() }) {
      std::filesystem::path p{ *file };
      if (p.is_relative()) { p = base_path.parent_path() / p; }
      result.source = local_source{ .file_path = p.lexically_normal() };
    } else {
      throw std::runtime_error("Recipe 'file' field must be string");
    }
  } else {
    throw std::runtime_error("Recipe must specify either 'url' or 'file'");
  }

  auto const options_it{ table->find("options") };
  if (options_it != table->end()) {
    if (auto const *options_table{ options_it->second.get<lua_table>() }) {
      for (auto const &[key, val] : *options_table) {
        if (auto const *val_str{ val.get<std::string>() }) {
          result.options[key] = *val_str;
        } else {
          throw std::runtime_error("Option value for '" + key + "' must be string");
        }
      }
    } else {
      throw std::runtime_error("Recipe 'options' field must be table");
    }
  }

  return result;
}

bool recipe::cfg::is_remote() const {
  return std::holds_alternative<remote_source>(source);
}

bool recipe::cfg::is_local() const { return std::holds_alternative<local_source>(source); }

recipe::recipe(cfg cfg, lua_state_ptr lua_state, std::vector<recipe *> dependencies)
    : m{ std::make_unique<impl>(std::move(cfg),
                                std::move(lua_state),
                                std::move(dependencies)) } {}

recipe::~recipe() = default;

recipe::cfg const &recipe::config() const { return m->cfg_; }

std::string const &recipe::identity() const { return m->cfg_.identity; }

std::string_view recipe::namespace_name() const {
  auto const dot_pos{ m->cfg_.identity.find('.') };
  if (dot_pos == std::string::npos) { return {}; }
  return std::string_view{ m->cfg_.identity.data(), dot_pos };
}

std::string_view recipe::name() const {
  auto const dot_pos{ m->cfg_.identity.find('.') };
  auto const at_pos{ m->cfg_.identity.find('@') };
  if (dot_pos == std::string::npos || at_pos == std::string::npos || dot_pos >= at_pos) {
    return {};
  }
  return std::string_view{ m->cfg_.identity.data() + dot_pos + 1, at_pos - dot_pos - 1 };
}

std::string_view recipe::version() const {
  auto const at_pos{ m->cfg_.identity.find('@') };
  if (at_pos == std::string::npos || at_pos == m->cfg_.identity.size() - 1) { return {}; }
  return std::string_view{ m->cfg_.identity.data() + at_pos + 1,
                           m->cfg_.identity.size() - at_pos - 1 };
}

recipe::cfg::source_t const &recipe::source() const { return m->cfg_.source; }

lua_State *recipe::lua_state() const { return m->lua_state_.get(); }

std::vector<recipe *> const &recipe::dependencies() const { return m->dependencies_; }

namespace {

std::string make_canonical_key(
    std::string const &identity,
    std::unordered_map<std::string, std::string> const &options) {
  if (options.empty()) { return identity; }

  std::vector<std::pair<std::string, std::string>> sorted{ options.begin(),
                                                           options.end() };
  std::ranges::sort(sorted);

  std::string key{ identity + '{' };
  for (size_t i{}; i < sorted.size(); ++i) {
    if (i > 0) key += ',';
    key += sorted[i].first + '=' + sorted[i].second;
  }
  key += '}';
  return key;
}

void validate_recipe_source(recipe::cfg const &cfg) {
  if (cfg.identity.starts_with("local.") && !cfg.is_local()) {
    throw std::runtime_error("Recipe 'local.*' must have local source: " + cfg.identity);
  }
}

using memo_map_t = tbb::concurrent_hash_map<std::string, recipe *>;

struct resolver {
  cache &cache_;
  memo_map_t memo_;
  std::mutex storage_mutex_;
  std::set<std::unique_ptr<recipe>> storage_;
};

recipe *recipe_resolve_one(resolver &r, recipe::cfg const &cfg) {
  auto const key{ make_canonical_key(cfg.identity, cfg.options) };

  // Try to insert placeholder - if already exists, wait and return
  memo_map_t::accessor acc;
  if (!r.memo_.insert(acc, key)) {
    return acc->second;  // other is resolving or has resolved, block until value available
  }

  // Winner: acc holds write lock, proceed to resolve
  acc->second = nullptr;  // Placeholder while we resolve
  acc.release();          // Release lock so we don't block memo reads during resolution

  // Cycle detection
  if (std::ranges::find(resolution_stack, key) != resolution_stack.end()) {
    std::string cycle_path{ key };
    for (auto it{ resolution_stack.rbegin() }; it != resolution_stack.rend(); ++it) {
      cycle_path += " -> " + *it;
      if (*it == key) break;
    }
    r.memo_.erase(key);  // Remove placeholder
    throw std::runtime_error("Dependency cycle detected: " + cycle_path);
  }
  resolution_stack.push_back(key);

  try {  // Load recipe file
    auto const recipe_path{ std::visit(
        overload{ [&](recipe::cfg::remote_source const &remote) -> std::filesystem::path {
                   return r.cache_.ensure_recipe(cfg.identity).entry_path;
                 },
                  [&](recipe::cfg::local_source const &local) -> std::filesystem::path {
                    auto const uri_info{ uri_classify(local.file_path.string()) };
                    if (uri_info.scheme == uri_scheme::LOCAL_FILE_RELATIVE) {
                      return uri_resolve_local_file_relative(local.file_path.string(),
                                                             std::nullopt);
                    }
                    return local.file_path;
                  } },
        cfg.source) };

    auto lua_state{ lua_make() };
    lua_add_envy(lua_state);

    if (!lua_run_file(lua_state, recipe_path)) {
      throw std::runtime_error("Failed to load recipe: " + cfg.identity);
    }

    // Extract and validate dependencies
    std::vector<recipe::cfg> dep_cfgs;

    if (auto const deps_array{ lua_global_to_array(lua_state.get(), "dependencies") }) {
      bool const allow_local_deps{ cfg.is_local() };
      dep_cfgs.reserve(deps_array->size());
      for (auto const &dep_val : *deps_array) {
        auto dep_cfg{ recipe::cfg::parse(dep_val, recipe_path) };
        if (!allow_local_deps && dep_cfg.identity.starts_with("local.")) {
          throw std::runtime_error("Non-local recipe cannot depend on local recipe: " +
                                   dep_cfg.identity);
        }
        dep_cfgs.push_back(std::move(dep_cfg));
      }
    }

    // Resolve children in parallel using nested task_group
    std::vector<recipe *> children(dep_cfgs.size());
    tbb::task_group child_tg;
    for (size_t i{}; i < dep_cfgs.size(); ++i) {
      child_tg.run([&, i, dep_cfg = dep_cfgs[i]]() {
        children[i] = recipe_resolve_one(r, dep_cfg);
      });
    }
    child_tg.wait();

    resolution_stack.pop_back();

    // Store recipe
    auto recipe_ptr{
      std::make_unique<recipe>(cfg, std::move(lua_state), std::move(children))
    };
    recipe *result{ recipe_ptr.get() };

    {
      std::lock_guard lock{ r.storage_mutex_ };
      r.storage_.insert(std::move(recipe_ptr));
    }

    // Update memo with resolved recipe (unblocks waiters)
    memo_map_t::accessor write_acc;
    if (r.memo_.find(write_acc, key)) { write_acc->second = result; }

    return result;
  } catch (...) {
    resolution_stack.pop_back();
    r.memo_.erase(key);  // Remove placeholder on error
    throw;
  }
}

}  // namespace

resolution_result recipe_resolve(std::vector<recipe::cfg> const &packages, cache &c) {
  for (auto const &pkg : packages) { validate_recipe_source(pkg); }

  resolver r{ .cache_ = c };
  tbb::task_group tg;
  std::vector<recipe *> roots(packages.size());

  for (size_t i{}; i < packages.size(); ++i) {
    tg.run([&, i]() { roots[i] = recipe_resolve_one(r, packages[i]); });
  }
  tg.wait();

  return { .recipes = std::move(r.storage_), .roots = roots };
}

}  // namespace envy

#include "recipe.h"

#include "lua_util.h"
#include "tui.h"
#include "uri.h"

#include "tbb/task_group.h"

#include <algorithm>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

namespace envy {
namespace {

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

recipe::recipe(cfg cfg, lua_state_ptr lua_state, std::vector<recipe *> dependencies)
    : cfg_{ std::move(cfg) },
      lua_state_{ std::move(lua_state) },
      dependencies_{ std::move(dependencies) } {}

std::string_view recipe::namespace_name() const {
  auto const dot_pos{ cfg_.identity.find('.') };
  if (dot_pos == std::string::npos) { return {}; }
  return std::string_view{ cfg_.identity.data(), dot_pos };
}

std::string_view recipe::name() const {
  auto const dot_pos{ cfg_.identity.find('.') };
  auto const at_pos{ cfg_.identity.find('@') };
  if (dot_pos == std::string::npos || at_pos == std::string::npos || dot_pos >= at_pos) {
    return {};
  }
  return std::string_view{ cfg_.identity.data() + dot_pos + 1, at_pos - dot_pos - 1 };
}

std::string_view recipe::version() const {
  auto const at_pos{ cfg_.identity.find('@') };
  if (at_pos == std::string::npos || at_pos == cfg_.identity.size() - 1) { return {}; }
  return std::string_view{ cfg_.identity.data() + at_pos + 1,
                           cfg_.identity.size() - at_pos - 1 };
}

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
  if (cfg.identity.starts_with("local.") &&
      !std::holds_alternative<recipe::cfg::local_source>(cfg.source)) {
    throw std::runtime_error("Recipe 'local.*' must have local source: " + cfg.identity);
  }
}

struct resolver {
  cache &cache_;
  std::mutex mutex_;
  std::unordered_map<std::string, recipe *> memo_;
  std::set<std::unique_ptr<recipe>> storage_;
};

recipe *recipe_resolve_one(resolver &r,
                           recipe::cfg const &cfg,
                           tbb::task_group &tg,
                           std::vector<std::string> stack) {
  auto const key{ make_canonical_key(cfg.identity, cfg.options) };

  {  // Check memo
    std::lock_guard lock{ r.mutex_ };
    if (auto const it{ r.memo_.find(key) }; it != r.memo_.end()) { return it->second; }
  }

  if (std::ranges::find(stack, key) != stack.end()) {  // Cycle detection
    throw std::runtime_error("Dependency cycle detected: " + key);
  }
  stack.push_back(key);

  // Load recipe file
  auto lua_state{ lua_make() };
  lua_add_envy(lua_state);

  auto const recipe_path{ std::visit(
      [&](auto const &source) -> std::filesystem::path {
        using T = std::decay_t<decltype(source)>;
        if constexpr (std::is_same_v<T, recipe::cfg::remote_source>) {
          return r.cache_.ensure_recipe(cfg.identity).entry_path;
        } else {  // local_source
          auto const uri_info{ uri_classify(source.file_path.string()) };
          if (uri_info.scheme == uri_scheme::LOCAL_FILE_RELATIVE) {
            return uri_resolve_local_file_relative(source.file_path.string(),
                                                   std::nullopt);
          }
          return source.file_path;
        }
      },
      cfg.source) };

  if (!lua_run_file(lua_state, recipe_path)) {
    throw std::runtime_error("Failed to load recipe: " + cfg.identity);
  }

  // Extract and validate dependencies
  std::vector<recipe::cfg> dep_cfgs;

  if (auto const deps_array{ lua_global_to_array(lua_state.get(), "dependencies") }) {
    auto const is_local{ std::holds_alternative<recipe::cfg::local_source>(cfg.source) };
    for (auto const &dep_val : *deps_array) {
      auto dep_cfg{ recipe::cfg::parse(dep_val, std::filesystem::path{}) };
      validate_recipe_source(dep_cfg);
      if (!is_local && dep_cfg.identity.starts_with("local.")) {
        throw std::runtime_error("Non-local recipe cannot depend on local recipe: " +
                                 dep_cfg.identity);
      }
      dep_cfgs.push_back(std::move(dep_cfg));
    }
  }

  // Resolve children in parallel
  std::vector<recipe *> children(dep_cfgs.size());
  for (size_t i{}; i < dep_cfgs.size(); ++i) {
    tg.run([&, i, dep_cfg = dep_cfgs[i], stack]() {
      children[i] = recipe_resolve_one(r, dep_cfg, tg, stack);
    });
  }
  tg.wait();

  // Store and memoize
  auto recipe_ptr{
    std::make_unique<recipe>(cfg, std::move(lua_state), std::move(children))
  };
  recipe *result{ recipe_ptr.get() };

  {
    std::lock_guard lock{ r.mutex_ };
    r.storage_.insert(std::move(recipe_ptr));
    r.memo_[key] = result;
  }

  return result;
}

}  // namespace

resolution_result recipe_resolve(std::vector<recipe::cfg> const &packages, cache &c) {
  for (auto const &pkg : packages) { validate_recipe_source(pkg); }

  resolver r{ .cache_ = c };
  tbb::task_group tg;
  std::vector<recipe *> roots(packages.size());

  for (size_t i{}; i < packages.size(); ++i) {
    tg.run([&, i]() { roots[i] = recipe_resolve_one(r, packages[i], tg, {}); });
  }
  tg.wait();

  return { .recipes = std::move(r.storage_), .roots = roots };
}

}  // namespace envy

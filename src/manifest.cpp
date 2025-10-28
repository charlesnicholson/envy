#include "manifest.h"

#include "lua_util.h"
#include "tui.h"

#include <algorithm>
#include <sstream>
#include <stdexcept>

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


// Deep compare options maps
bool options_equal(std::unordered_map<std::string, std::string> const &a,
                   std::unordered_map<std::string, std::string> const &b) {
  if (a.size() != b.size()) { return false; }
  for (auto const &[key, val] : a) {
    auto const it{ b.find(key) };
    if (it == b.end() || it->second != val) { return false; }
  }
  return true;
}

// Deep compare sources
bool source_equal(recipe::source_t const &a, recipe::source_t const &b) {
  if (a.index() != b.index()) { return false; }

  if (auto const *ra{ std::get_if<recipe::remote_source>(&a) }) {
    auto const *rb{ std::get_if<recipe::remote_source>(&b) };
    return ra->url == rb->url && ra->sha256 == rb->sha256;
  }

  if (auto const *la{ std::get_if<recipe::local_source>(&a) }) {
    auto const *lb{ std::get_if<recipe::local_source>(&b) };
    return la->file_path == lb->file_path;
  }

  return true;  // builtin_source
}

recipe_spec parse_package(lua_value const &entry, std::filesystem::path const &base_path) {
  recipe_spec spec;

  // Shorthand string: "namespace.name@version"
  if (auto const *str{ entry.get<std::string>() }) {
    spec.identity = *str;

    // Validate identity format
    std::string ns, name, ver;
    if (!parse_identity(spec.identity, ns, name, ver)) {
      throw std::runtime_error("Invalid recipe identity format: " + spec.identity);
    }

    spec.source = recipe::builtin_source{};
    return spec;
  }

  auto const *table{ entry.get<lua_table>() };
  if (!table) { throw std::runtime_error("Package entry must be string or table"); }

  auto const recipe_it{ table->find("recipe") };
  if (recipe_it == table->end()) {
    throw std::runtime_error("Package table missing required 'recipe' field");
  }
  auto const *recipe_str{ recipe_it->second.get<std::string>() };
  if (!recipe_str) { throw std::runtime_error("Package 'recipe' field must be string"); }
  spec.identity = *recipe_str;

  std::string ns, name, ver;
  if (!parse_identity(spec.identity, ns, name, ver)) {
    throw std::runtime_error("Invalid recipe identity format: " + spec.identity);
  }

  auto const url_it{ table->find("url") };
  auto const file_it{ table->find("file") };

  if (url_it != table->end() && file_it != table->end()) {
    throw std::runtime_error("Package cannot specify both 'url' and 'file'");
  }

  if (url_it != table->end()) {
    // Remote source
    auto const *url_str{ url_it->second.get<std::string>() };
    if (!url_str) { throw std::runtime_error("Package 'url' field must be string"); }

    auto const sha256_it{ table->find("sha256") };
    if (sha256_it == table->end()) {
      throw std::runtime_error("Package with 'url' must specify 'sha256'");
    }
    auto const *sha256_str{ sha256_it->second.get<std::string>() };
    if (!sha256_str) { throw std::runtime_error("Package 'sha256' field must be string"); }

    spec.source = recipe::remote_source{ .url = *url_str, .sha256 = *sha256_str };
  } else if (file_it != table->end()) {
    // Local source
    auto const *file_str{ file_it->second.get<std::string>() };
    if (!file_str) { throw std::runtime_error("Package 'file' field must be string"); }

    std::filesystem::path file_path{ *file_str };
    if (file_path.is_relative()) { file_path = base_path.parent_path() / file_path; }
    spec.source = recipe::local_source{ .file_path = file_path.lexically_normal() };
  } else {
    // No source specified, assume builtin
    spec.source = recipe::builtin_source{};
  }

  auto const options_it{ table->find("options") };
  if (options_it != table->end()) {
    auto const *options_table{ options_it->second.get<lua_table>() };
    if (!options_table) {
      throw std::runtime_error("Package 'options' field must be table");
    }

    for (auto const &[key, val] : *options_table) {
      auto const *val_str{ val.get<std::string>() };
      if (!val_str) {
        throw std::runtime_error("Option value for '" + key + "' must be string");
      }
      spec.options[key] = *val_str;
    }
  }

  return spec;
}

recipe_override parse_override(lua_value const &entry,
                               std::filesystem::path const &base_path) {
  auto const *table{ entry.get<lua_table>() };
  if (!table) { throw std::runtime_error("Override entry must be table"); }

  auto const url_it{ table->find("url") };
  auto const file_it{ table->find("file") };

  if (url_it != table->end() && file_it != table->end()) {
    throw std::runtime_error("Override cannot specify both 'url' and 'file'");
  }

  if (url_it != table->end()) {
    auto const *url_str{ url_it->second.get<std::string>() };
    if (!url_str) { throw std::runtime_error("Override 'url' field must be string"); }

    auto const sha256_it{ table->find("sha256") };
    if (sha256_it == table->end()) {
      throw std::runtime_error("Override with 'url' must specify 'sha256'");
    }
    auto const *sha256_str{ sha256_it->second.get<std::string>() };
    if (!sha256_str) {
      throw std::runtime_error("Override 'sha256' field must be string");
    }

    return recipe::remote_source{ .url = *url_str, .sha256 = *sha256_str };
  }

  if (file_it != table->end()) {
    auto const *file_str{ file_it->second.get<std::string>() };
    if (!file_str) { throw std::runtime_error("Override 'file' field must be string"); }

    std::filesystem::path file_path{ *file_str };
    if (file_path.is_relative()) { file_path = base_path.parent_path() / file_path; }
    return recipe::local_source{ .file_path = file_path.lexically_normal() };
  }

  throw std::runtime_error("Override must specify 'url'+'sha256' or 'file'");
}


manifest load_from_string_impl(lua_state_ptr const &state,
                               std::filesystem::path const &base_path) {
  manifest m;
  m.manifest_path = base_path;

  auto packages_array{ lua_global_to_array(state.get(), "packages") };

  if (packages_array.empty()) {
    lua_getglobal(state.get(), "packages");
    bool const exists{ !lua_isnil(state.get(), -1) };
    lua_pop(state.get(), 1);

    if (!exists) {
      throw std::runtime_error("Manifest must define 'packages' global");
    }
  }

  for (auto const &val : packages_array) {
    m.packages.push_back(parse_package(val, base_path));
  }

  auto overrides_value{ lua_global_to_value(state.get(), "overrides") };
  if (overrides_value) {
    auto const *overrides_table{ overrides_value->get<lua_table>() };
    if (!overrides_table) { throw std::runtime_error("'overrides' must be a table"); }

    for (auto const &[identity, val] : *overrides_table) {
      // Validate identity format
      std::string ns, name, ver;
      if (!parse_identity(identity, ns, name, ver)) {
        throw std::runtime_error("Invalid override identity format: " + identity);
      }

      m.overrides[identity] = parse_override(val, base_path);
    }
  }

  // Validation: detect duplicate (identity, options) and conflicting sources
  for (size_t i{ 0 }; i < m.packages.size(); ++i) {
    for (size_t j{ i + 1 }; j < m.packages.size(); ++j) {
      if (m.packages[i].identity == m.packages[j].identity &&
          options_equal(m.packages[i].options, m.packages[j].options)) {
        // Same identity and options - check if sources match
        if (!source_equal(m.packages[i].source, m.packages[j].source)) {
          throw std::runtime_error("Conflicting sources for package: " +
                                   m.packages[i].identity);
        } else {
          throw std::runtime_error("Duplicate package entry: " + m.packages[i].identity);
        }
      }
    }
  }

  return m;
}

}  // namespace

std::optional<std::filesystem::path> manifest::discover() {
  namespace fs = std::filesystem;

  auto cur{ fs::current_path() };

  for (;;) {
    auto const manifest_path{ cur / "envy.lua" };
    if (fs::exists(manifest_path)) { return manifest_path; }

    auto const git_path{ cur / ".git" };
    if (fs::exists(git_path) && fs::is_directory(git_path)) { return std::nullopt; }

    auto const parent{ cur.parent_path() };
    if (parent == cur) { return std::nullopt; }

    cur = parent;
  }
}

manifest manifest::load(char const *script, std::filesystem::path const &manifest_path) {
  auto state{ lua_make() };
  if (!state) { throw std::runtime_error("Failed to create Lua state"); }

  lua_add_envy(state);

  if (!lua_run_string(state, script)) {
    throw std::runtime_error("Failed to execute manifest script");
  }

  return load_from_string_impl(state, manifest_path);
}

}  // namespace envy

#include "manifest.h"

#include "lua_util.h"

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

recipe_spec parse_recipe_spec(lua_value const &recipe_spec_lua,
                              std::filesystem::path const &base_path) {
  recipe_spec spec;

  //  "namespace.name@version"
  if (auto const *str{ recipe_spec_lua.get<std::string>() }) {
    spec.identity = *str;

    std::string ns, name, ver;
    if (!parse_identity(spec.identity, ns, name, ver)) {
      throw std::runtime_error("Invalid recipe identity format: " + spec.identity);
    }

    spec.source = recipe::builtin_source{};
    return spec;
  }

  auto const *table{ recipe_spec_lua.get<lua_table>() };
  if (!table) { throw std::runtime_error("Package entry must be string or table"); }

  spec.identity = [&] {
    auto const recipe_it{ table->find("recipe") };
    if (recipe_it == table->end()) {
      throw std::runtime_error("Package table missing required 'recipe' field");
    }
    auto const *recipe_str{ recipe_it->second.get<std::string>() };
    if (!recipe_str) { throw std::runtime_error("Package 'recipe' field must be string"); }
    return *recipe_str;
  }();

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
    if (auto const *url{ url_it->second.get<std::string>() }) {
      auto const sha256_it{ table->find("sha256") };
      if (sha256_it == table->end()) {
        throw std::runtime_error("Package with 'url' must specify 'sha256'");
      }
      if (auto const *sha256{ sha256_it->second.get<std::string>() }) {
        spec.source = recipe::remote_source{ .url = *url, .sha256 = *sha256 };
      } else {
        throw std::runtime_error("Package 'sha256' field must be string");
      }
    } else {
      throw std::runtime_error("Package 'url' field must be string");
    }
  } else if (file_it != table->end()) {
    // Local source
    if (auto const *file{ file_it->second.get<std::string>() }) {
      spec.source = recipe::local_source{ .file_path = [&] {
        std::filesystem::path p{ *file };
        if (p.is_relative()) { p = base_path.parent_path() / p; }
        return p.lexically_normal();
      }() };
    } else {
      throw std::runtime_error("Package 'file' field must be string");
    }
  } else {
    // No source specified, assume builtin
    spec.source = recipe::builtin_source{};
  }

  auto const options_it{ table->find("options") };
  if (options_it != table->end()) {
    if (auto const *options_table{ options_it->second.get<lua_table>() }) {
      for (auto const &[key, val] : *options_table) {
        if (auto const *val_str{ val.get<std::string>() }) {
          spec.options[key] = *val_str;
        } else {
          throw std::runtime_error("Option value for '" + key + "' must be string");
        }
      }
    } else {
      throw std::runtime_error("Package 'options' field must be table");
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
    if (auto const *url{ url_it->second.get<std::string>() }) {
      auto const sha256_it{ table->find("sha256") };
      if (sha256_it == table->end()) {
        throw std::runtime_error("Override with 'url' must specify 'sha256'");
      }
      if (auto const *sha256{ sha256_it->second.get<std::string>() }) {
        return recipe::remote_source{ .url = *url, .sha256 = *sha256 };
      } else {
        throw std::runtime_error("Override 'sha256' field must be string");
      }
    } else {
      throw std::runtime_error("Override 'url' field must be string");
    }
  }

  if (file_it != table->end()) {
    if (auto const *file{ file_it->second.get<std::string>() }) {
      return recipe::local_source{ .file_path = [&] {
        std::filesystem::path p{ *file };
        if (p.is_relative()) { p = base_path.parent_path() / p; }
        return p.lexically_normal();
      }() };
    } else {
      throw std::runtime_error("Override 'file' field must be string");
    }
  }

  throw std::runtime_error("Override must specify 'url'+'sha256' or 'file'");
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

  manifest m;
  m.manifest_path = manifest_path;

  auto packages{ lua_global_to_array(state.get(), "packages") };
  if (!packages) { throw std::runtime_error("Manifest must define 'packages' global"); }

  for (auto const &package : *packages) {
    m.packages.push_back(parse_recipe_spec(package, manifest_path));
  }

  auto overrides{ lua_global_to_value(state.get(), "overrides") };
  if (overrides) {
    if (auto const *overrides_table{ overrides->get<lua_table>() }) {
      for (auto const &[identity, val] : *overrides_table) {
        // Validate identity format
        std::string ns, name, ver;
        if (!parse_identity(identity, ns, name, ver)) {
          throw std::runtime_error("Invalid override identity format: " + identity);
        }

        m.overrides[identity] = parse_override(val, manifest_path);
      }
    } else {
      throw std::runtime_error("'overrides' must be a table");
    }
  }

  return m;
}

}  // namespace envy

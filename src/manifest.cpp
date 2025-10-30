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
        return recipe::cfg::remote_source{ .url = *url, .sha256 = *sha256 };
      } else {
        throw std::runtime_error("Override 'sha256' field must be string");
      }
    } else {
      throw std::runtime_error("Override 'url' field must be string");
    }
  }

  if (file_it != table->end()) {
    if (auto const *file{ file_it->second.get<std::string>() }) {
      std::filesystem::path p{ *file };
      if (p.is_relative()) { p = base_path.parent_path() / p; }
      return recipe::cfg::local_source{ .file_path = p.lexically_normal() };
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
    m.packages.push_back(recipe::cfg::parse(package, manifest_path));
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

#include "recipe_spec.h"

#include "uri.h"

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

}  // namespace

recipe recipe::parse(lua_value const &lua_val, std::filesystem::path const &base_path) {
  recipe result;

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
        // Resolve relative file:// URLs relative to base_path
        std::string resolved_url{ *url };
        auto const info{ uri_classify(resolved_url) };
        if (info.scheme == uri_scheme::LOCAL_FILE_RELATIVE) {
          std::filesystem::path p{ info.canonical };
          p = base_path.parent_path() / p;
          resolved_url = p.lexically_normal().string();
        }
        result.source = remote_source{ .url = resolved_url, .sha256 = *sha256 };
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

bool recipe::is_remote() const { return std::holds_alternative<remote_source>(source); }
bool recipe::is_local() const { return std::holds_alternative<local_source>(source); }
bool recipe::is_git() const { return std::holds_alternative<git_source>(source); }

bool recipe::has_fetch_function() const {
  return std::holds_alternative<fetch_function>(source);
}

}  // namespace envy

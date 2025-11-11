#include "recipe_spec.h"

#include "uri.h"

#include <algorithm>
#include "platform.h"
#include <charconv>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <vector>

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

recipe_spec recipe_spec::parse(lua_value const &lua_val,
                               std::filesystem::path const &base_path) {
  recipe_spec result;

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
      // Resolve relative file:// URLs relative to base_path
      std::string resolved_url{ *url };
      auto const info{ uri_classify(resolved_url) };
      if (info.scheme == uri_scheme::LOCAL_FILE_RELATIVE) {
        std::filesystem::path p{ info.canonical };
        p = base_path.parent_path() / p;
        resolved_url = p.lexically_normal().string();
      }

      // SHA256 is optional (permissive mode)
      std::string sha256_value;
      auto const sha256_it{ table->find("sha256") };
      if (sha256_it != table->end()) {
        if (auto const *sha256{ sha256_it->second.get<std::string>() }) {
          sha256_value = *sha256;
        } else {
          throw std::runtime_error("Recipe 'sha256' field must be string");
        }
      }

      result.source = remote_source{ .url = resolved_url, .sha256 = sha256_value };
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

  if (auto const options_it{ table->find("options") }; options_it != table->end()) {
    if (auto const *options_table{ options_it->second.get<lua_table>() }) {
      for (auto const &[key, val] : *options_table) { result.options[key] = val; }
    } else {
      throw std::runtime_error("Recipe 'options' field must be table");
    }
  }

  return result;
}

bool recipe_spec::is_git() const { return std::holds_alternative<git_source>(source); }
bool recipe_spec::is_local() const { return std::holds_alternative<local_source>(source); }
bool recipe_spec::is_remote() const {
  return std::holds_alternative<remote_source>(source);
}

bool recipe_spec::has_fetch_function() const {
  return std::holds_alternative<fetch_function>(source);
}

std::string recipe_spec::serialize_option_table(lua_value const &val) {
  static_assert(
      std::is_same_v<std::variant_alternative_t<0, lua_variant>, std::monostate>);
  static_assert(std::is_same_v<std::variant_alternative_t<1, lua_variant>, bool>);
  static_assert(std::is_same_v<std::variant_alternative_t<2, lua_variant>, int64_t>);
  static_assert(std::is_same_v<std::variant_alternative_t<3, lua_variant>, double>);
  static_assert(std::is_same_v<std::variant_alternative_t<4, lua_variant>, std::string>);
  static_assert(std::is_same_v<std::variant_alternative_t<5, lua_variant>, lua_table>);

  switch (val.v.index()) {
    case 0: return "nil";
    case 1: return std::get<bool>(val.v) ? "true" : "false";
    case 2: return std::to_string(std::get<int64_t>(val.v));

    case 3: {
      char buf[32];
      auto [ptr, ec] = std::to_chars(buf,
                                     buf + sizeof(buf),
                                     std::get<double>(val.v),
                                     std::chars_format::general);
      if (ec != std::errc{}) {
        throw std::runtime_error("Failed to serialize double value");
      }
      return std::string{ buf, ptr };
    }

    case 4: {
      auto const &str{ std::get<std::string>(val.v) };
      std::string result;
      result.reserve(str.size() + 2);
      result += '"';
      for (char c : str) {
        if (c == '"' || c == '\\') { result += '\\'; }
        result += c;
      }
      result += '"';
      return result;
    }

    case 5: {
      auto const &table{ std::get<lua_table>(val.v) };
      if (table.empty()) { return "{}"; }

      std::vector<std::pair<std::string, std::string>> sorted;
      sorted.reserve(table.size());
      for (auto const &[key, value] : table) {
        sorted.emplace_back(key, recipe_spec::serialize_option_table(value));
      }
      std::ranges::sort(sorted);

      std::ostringstream oss;
      oss << '{';
      bool first{ true };
      for (auto const &[key, serialized_val] : sorted) {
        if (!first) { oss << ','; };
        oss << key << '=' << serialized_val;
        first = false;
      }
      oss << '}';
      return oss.str();
    }
  }

  ENVY_UNREACHABLE();
}

}  // namespace envy

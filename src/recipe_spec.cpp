#include "recipe_spec.h"

#include "uri.h"

#include <algorithm>
#include <charconv>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <vector>
#include "platform.h"

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

  auto const source_it{ table->find("source") };

  if (source_it == table->end()) {
    throw std::runtime_error("Recipe must specify 'source' field");
  }

  if (auto const *source_uri{ source_it->second.get<std::string>() }) {
    auto const info{ uri_classify(*source_uri) };

    // Check if this is a git repository
    if (info.scheme == uri_scheme::GIT) {
      auto const *ref_str{ [&]() -> std::string const * {
        auto const ref_it{ table->find("ref") };
        if (ref_it == table->end()) {
          throw std::runtime_error("Recipe with git source must specify 'ref' field");
        }
        auto const *ref{ ref_it->second.get<std::string>() };
        if (!ref) { throw std::runtime_error("Recipe 'ref' field must be string"); }
        if (ref->empty()) {
          throw std::runtime_error("Recipe 'ref' field cannot be empty");
        }
        return ref;
      }() };

      result.source = git_source{ .url = info.canonical, .ref = *ref_str };
    } else {
      auto const sha256{ [&]() -> std::optional<std::string> {
        auto const sha256_it{ table->find("sha256") };
        if (sha256_it == table->end()) { return std::nullopt; }
        if (auto const *sha256_str{ sha256_it->second.get<std::string>() }) {
          return *sha256_str;
        }
        throw std::runtime_error("Recipe 'sha256' field must be string");
      }() };

      // If SHA256 is provided, always treat as remote_source (needs verification)
      // Otherwise, local files use local_source, remote URIs use remote_source
      if (sha256.has_value() || (info.scheme != uri_scheme::LOCAL_FILE_ABSOLUTE &&
                                 info.scheme != uri_scheme::LOCAL_FILE_RELATIVE)) {
        // Remote source or local file with SHA256 verification
        std::string const resolved_uri{ [&]() -> std::string {
          if (info.scheme == uri_scheme::LOCAL_FILE_RELATIVE) {
            std::filesystem::path p{ info.canonical };
            p = base_path.parent_path() / p;
            return "file://" + p.lexically_normal().string();
          } else if (info.scheme == uri_scheme::LOCAL_FILE_ABSOLUTE) {
            return "file://" + info.canonical;
          } else {
            return info.canonical;
          }
        }() };
        result.source =
            remote_source{ .url = resolved_uri, .sha256 = sha256.value_or("") };
      } else {
        // Local file without SHA256 - use local_source (no verification)
        std::filesystem::path p{ info.canonical };
        if (info.scheme == uri_scheme::LOCAL_FILE_RELATIVE) {
          p = base_path.parent_path() / p;
          p = p.lexically_normal();
        }
        result.source = local_source{ .file_path = p };
      }
    }
  } else {
    throw std::runtime_error("Recipe 'source' field must be string");
  }

  if (auto const options_it{ table->find("options") }; options_it != table->end()) {
    if (auto const *options_table{ options_it->second.get<lua_table>() }) {
      for (auto const &[key, val] : *options_table) { result.options[key] = val; }
    } else {
      throw std::runtime_error("Recipe 'options' field must be table");
    }
  }

  if (auto const needed_by_it{ table->find("needed_by") }; needed_by_it != table->end()) {
    if (auto const *needed_by_str{ needed_by_it->second.get<std::string>() }) {
      if (*needed_by_str == "check") {
        result.needed_by = recipe_phase::asset_check;
      } else if (*needed_by_str == "fetch") {
        result.needed_by = recipe_phase::asset_fetch;
      } else if (*needed_by_str == "stage") {
        result.needed_by = recipe_phase::asset_stage;
      } else if (*needed_by_str == "build") {
        result.needed_by = recipe_phase::asset_build;
      } else if (*needed_by_str == "install") {
        result.needed_by = recipe_phase::asset_install;
      } else if (*needed_by_str == "deploy") {
        result.needed_by = recipe_phase::asset_deploy;
      } else {
        throw std::runtime_error(
            "Recipe 'needed_by' must be one of: check, fetch, stage, "
            "build, install, deploy (got: " +
            *needed_by_str + ")");
      }
    } else {
      throw std::runtime_error("Recipe 'needed_by' field must be string");
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

std::string recipe_spec::format_key(
    std::string const &identity,
    std::unordered_map<std::string, lua_value> const &options) {
  if (options.empty()) { return identity; }

  // Wrap options in lua_value and use serialize_option_table for consistency
  lua_value options_as_value{ lua_variant{ options } };
  return identity + serialize_option_table(options_as_value);
}

std::string recipe_spec::format_key() const { return format_key(identity, options); }
}  // namespace envy

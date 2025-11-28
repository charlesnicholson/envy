#include "recipe_spec.h"

#include "uri.h"

#include <algorithm>
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

bool contains_function(sol::object const &val) {
  if (val.is<sol::function>()) { return true; }
  if (val.is<sol::table>()) {
    sol::table tbl{ val.as<sol::table>() };
    for (auto const &[key, nested_val] : tbl) {
      sol::object nested_obj(nested_val);
      if (contains_function(nested_obj)) { return true; }
    }
  }
  return false;
}

// Parse source table (custom source fetch with dependencies)
recipe_spec::source_t parse_source_table(sol::table const &source_table,
                                         std::filesystem::path const &base_path,
                                         std::vector<recipe_spec> &out_dependencies,
                                         bool allow_weak_without_source) {
  // Check for dependencies field (need to parse as array)
  bool has_dependencies{ false };
  sol::object deps_obj{ source_table["dependencies"] };
  if (deps_obj.valid()) {
    if (deps_obj.is<sol::table>()) {
      sol::table deps_table{ deps_obj.as<sol::table>() };
      has_dependencies = true;
      // Iterate Sol2 table by numeric indices
      for (size_t i{ 1 };; ++i) {
        sol::object dep_obj{ deps_table[i] };
        if (!dep_obj.valid()) { break; }
        out_dependencies.push_back(
            recipe_spec::parse(dep_obj, base_path, allow_weak_without_source));
      }
    } else {
      throw std::runtime_error("source.dependencies must be array (table)");
    }
  }

  // Check for fetch function
  bool const has_fetch{ [&]() {
    sol::object fetch_obj{ source_table["fetch"] };
    if (fetch_obj.valid()) {
      if (!fetch_obj.is<sol::function>()) {
        throw std::runtime_error("source.fetch must be a function");
      }
      return true;
    }
    return false;
  }() };

  // Validation: dependencies requires fetch, fetch can exist alone
  if (has_dependencies && !has_fetch) {
    throw std::runtime_error("source.dependencies requires source.fetch function");
  }

  if (!has_dependencies && !has_fetch) {
    throw std::runtime_error(
        "source table must have either URL string or dependencies+fetch function");
  }

  // Custom source fetch - no URL-based source
  return recipe_spec::fetch_function{};
}

// Parse source string (URI-based sources)
recipe_spec::source_t parse_source_string(std::string const &source_uri,
                                          sol::table const &table,
                                          std::filesystem::path const &base_path) {
  auto const info{ uri_classify(source_uri) };

  // Check if this is a git repository
  if (info.scheme == uri_scheme::GIT) {
    std::string const ref_str{ [&]() -> std::string {
      sol::object ref_obj{ table["ref"] };
      if (!ref_obj.valid()) {
        throw std::runtime_error("Recipe with git source must specify 'ref' field");
      }
      if (!ref_obj.is<std::string>()) {
        throw std::runtime_error("Recipe 'ref' field must be string");
      }
      std::string ref{ ref_obj.as<std::string>() };
      if (ref.empty()) { throw std::runtime_error("Recipe 'ref' field cannot be empty"); }
      return ref;
    }() };

    return recipe_spec::git_source{ .url = info.canonical, .ref = ref_str };
  }

  auto const sha256{ [&]() -> std::optional<std::string> {
    sol::object sha256_obj{ table["sha256"] };
    if (!sha256_obj.valid()) { return std::nullopt; }
    if (!sha256_obj.is<std::string>()) {
      throw std::runtime_error("Recipe 'sha256' field must be string");
    }
    return sha256_obj.as<std::string>();
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
    return recipe_spec::remote_source{ .url = resolved_uri,
                                       .sha256 = sha256.value_or("") };
  }

  // Local file without SHA256 - use local_source (no verification)
  std::filesystem::path p{ info.canonical };
  if (info.scheme == uri_scheme::LOCAL_FILE_RELATIVE) {
    p = base_path.parent_path() / p;
    p = p.lexically_normal();
  }
  return recipe_spec::local_source{ .file_path = p };
}

}  // namespace

recipe_spec recipe_spec::parse(sol::object const &lua_val,
                               std::filesystem::path const &base_path,
                               bool const allow_weak_without_source) {
  recipe_spec result;

  //  "namespace.name@version" shorthand requires url or file
  if (lua_val.is<std::string>()) {
    throw std::runtime_error(
        "Recipe shorthand string syntax requires table with 'url' or 'file': " +
        lua_val.as<std::string>());
  }

  if (!lua_val.is<sol::table>()) {
    throw std::runtime_error("Recipe entry must be string or table");
  }

  sol::table table{ lua_val.as<sol::table>() };

  // Initialize serialized options to empty table
  result.serialized_options = "{}";

  result.identity = [&]() -> std::string {
    sol::object recipe_obj{ table["recipe"] };
    if (!recipe_obj.valid()) {
      throw std::runtime_error("Recipe table missing required 'recipe' field");
    }
    if (!recipe_obj.is<std::string>()) {
      throw std::runtime_error("Recipe 'recipe' field must be string");
    }
    std::string ident{ recipe_obj.as<std::string>() };
    if (ident.empty()) {
      throw std::runtime_error("Recipe 'recipe' field cannot be empty");
    }
    return ident;
  }();

  sol::object weak_obj{ table["weak"] };
  bool const has_weak{ weak_obj.valid() };

  sol::object source_obj{ table["source"] };
  bool const has_source{ source_obj.valid() };

  if (has_source && has_weak) {
    throw std::runtime_error("Recipe cannot specify both 'source' and 'weak' fields");
  }

  bool const allow_missing_source{ allow_weak_without_source && !has_source };

  std::string ns, name, ver;
  if (!allow_missing_source) {
    if (!parse_identity(result.identity, ns, name, ver)) {
      throw std::runtime_error("Invalid recipe identity format: " + result.identity);
    }
  }

  // Check if source is a table (custom source fetch with dependencies)
  if (has_source) {
    if (source_obj.is<sol::table>()) {
      sol::table source_table{ source_obj.as<sol::table>() };
      result.source = parse_source_table(source_table,
                                         base_path,
                                         result.source_dependencies,
                                         allow_weak_without_source);
    } else if (source_obj.is<std::string>()) {
      std::string source_uri{ source_obj.as<std::string>() };
      result.source = parse_source_string(source_uri, table, base_path);
    } else {
      throw std::runtime_error("Recipe 'source' field must be string or table");
    }
  } else {
    if (!allow_weak_without_source) {
      throw std::runtime_error("Recipe must specify 'source' field");
    }
    result.source = recipe_spec::weak_ref{};
  }

  // Check if options field exists and serialize it
  sol::object options_obj{ table["options"] };
  if (options_obj.valid() && options_obj.get_type() == sol::type::table) {
    // Validate options don't contain functions
    if (contains_function(options_obj)) {
      throw std::runtime_error("Unsupported Lua type: function");
    }
    // Serialize to Lua table literal
    result.serialized_options = serialize_option_table(options_obj);
  } else if (options_obj.valid() && options_obj.get_type() != sol::type::lua_nil) {
    throw std::runtime_error("Recipe 'options' field must be table");
  }

  sol::object needed_by_obj{ table["needed_by"] };
  if (needed_by_obj.valid()) {
    if (!needed_by_obj.is<std::string>()) {
      throw std::runtime_error("Recipe 'needed_by' field must be string");
    }
    std::string needed_by_str{ needed_by_obj.as<std::string>() };
    if (needed_by_str == "check") {
      result.needed_by = recipe_phase::asset_check;
    } else if (needed_by_str == "fetch") {
      result.needed_by = recipe_phase::asset_fetch;
    } else if (needed_by_str == "stage") {
      result.needed_by = recipe_phase::asset_stage;
    } else if (needed_by_str == "build") {
      result.needed_by = recipe_phase::asset_build;
    } else if (needed_by_str == "install") {
      result.needed_by = recipe_phase::asset_install;
    } else if (needed_by_str == "deploy") {
      result.needed_by = recipe_phase::asset_deploy;
    } else {
      throw std::runtime_error(
          "Recipe 'needed_by' must be one of: check, fetch, stage, "
          "build, install, deploy (got: " +
          needed_by_str + ")");
    }
  }

  if (has_weak) {
    if (!weak_obj.is<sol::table>()) {
      throw std::runtime_error("Recipe 'weak' field must be table");
    }
    // Weak fallback must be a strong spec; do not allow nested weak-without-source here
    result.weak =
        std::make_unique<recipe_spec>(recipe_spec::parse(weak_obj, base_path, false));
    if (result.weak->needed_by.has_value()) {
      throw std::runtime_error("weak fallback must not specify 'needed_by'");
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
  return holds_alternative<fetch_function>(source);
}

bool recipe_spec::is_weak_reference() const {
  return std::holds_alternative<weak_ref>(source);
}

std::string recipe_spec::serialize_option_table(sol::object const &val) {
  if (val.get_type() == sol::type::lua_nil) { return "nil"; }
  if (val.is<bool>()) { return val.as<bool>() ? "true" : "false"; }
  if (val.is<lua_Integer>()) { return std::to_string(val.as<lua_Integer>()); }
  if (val.is<lua_Number>()) {
    char buf[32];
    auto [ptr, ec] = std::to_chars(buf,
                                   buf + sizeof(buf),
                                   val.as<lua_Number>(),
                                   std::chars_format::general);
    if (ec != std::errc{}) {
      throw std::runtime_error("Failed to serialize double value");
    }
    return std::string{ buf, ptr };
  }

  if (val.is<std::string>()) {
    auto const &str{ val.as<std::string>() };
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

  if (val.is<sol::table>()) {
    sol::table table{ val.as<sol::table>() };
    if (table.empty()) { return "{}"; }

    std::vector<std::pair<std::string, std::string>> sorted;
    for (auto const &[key, value] : table) {
      sol::object key_obj(key);
      sol::object value_obj(value);
      if (key_obj.is<std::string>()) {
        sorted.emplace_back(key_obj.as<std::string>(),
                            recipe_spec::serialize_option_table(value_obj));
      }
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

  throw std::runtime_error("Unsupported Lua type in serialize_option_table");
}

std::string recipe_spec::format_key(std::string const &identity,
                                    std::string const &serialized_options) {
  if (serialized_options.empty() || serialized_options == "{}") { return identity; }
  return identity + serialized_options;
}

std::string recipe_spec::format_key() const {
  return format_key(identity, serialized_options);
}

recipe_spec recipe_spec::parse_from_stack(sol::state_view lua,
                                          int index,
                                          std::filesystem::path const &base_path,
                                          bool const allow_weak_without_source) {
  sol::stack_object stack_obj{ lua, index };
  sol::object recipe_val{ stack_obj };
  return parse(recipe_val, base_path, allow_weak_without_source);
}

bool recipe_spec::lookup_and_push_source_fetch(sol::state_view lua,
                                               std::string const &dep_identity) {
  // Look up the dependencies global array
  sol::object deps_obj{ lua["dependencies"] };
  if (!deps_obj.valid() || !deps_obj.is<sol::table>()) { return false; }

  sol::table deps_table{ deps_obj.as<sol::table>() };

  // Iterate through dependencies array to find matching identity
  for (size_t i{ 1 };; ++i) {
    sol::object dep_entry{ deps_table[i] };
    if (!dep_entry.valid()) { break; }  // End of array

    if (!dep_entry.is<sol::table>()) { continue; }
    sol::table dep_table{ dep_entry.as<sol::table>() };

    // Check if this entry has matching recipe identity
    sol::object recipe_obj{ dep_table["recipe"] };
    if (!recipe_obj.valid() || !recipe_obj.is<std::string>()) { continue; }

    std::string const identity{ recipe_obj.as<std::string>() };
    if (identity == dep_identity) {
      // Found matching dependency - get source.fetch
      sol::object source_obj{ dep_table["source"] };
      if (!source_obj.valid() || !source_obj.is<sol::table>()) { return false; }

      sol::table source_table{ source_obj.as<sol::table>() };
      sol::object fetch_obj{ source_table["fetch"] };
      if (!fetch_obj.valid() || !fetch_obj.is<sol::function>()) { return false; }

      // Push the fetch function onto the stack
      fetch_obj.push(lua.lua_state());
      return true;
    }
  }

  return false;
}

}  // namespace envy

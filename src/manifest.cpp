#include "manifest.h"

#include "engine.h"
#include "lua_ctx/lua_ctx_bindings.h"
#include "lua_envy.h"
#include "lua_shell.h"
#include "shell.h"
#include "sol_util.h"
#include "tui.h"

#include <cstring>
#include <stdexcept>

namespace envy {

namespace {

size_t skip_whitespace(std::string_view s, size_t pos) {
  while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t')) { ++pos; }
  return pos;
}

std::string_view parse_identifier(std::string_view s, size_t &pos) {
  size_t const start{ pos };
  while ((pos < s.size()) && (std::isalnum(static_cast<unsigned char>(s[pos])) ||
                              s[pos] == '_' || s[pos] == '-')) {
    ++pos;
  }
  return s.substr(start, pos - start);
}

// Expects pos to be at opening quote, advances pos past closing quote
std::optional<std::string> parse_quoted_value(std::string_view s, size_t &pos) {
  if (pos >= s.size() || s[pos] != '"') { return std::nullopt; }
  ++pos;  // skip opening quote

  std::string result;
  while (pos < s.size() && s[pos] != '"') {
    if (s[pos] == '\\' && pos + 1 < s.size()) {
      char const next{ s[pos + 1] };
      if (next == '"' || next == '\\') {
        result += next;
        pos += 2;
        continue;
      }
    }

    result += s[pos];
    ++pos;
  }

  if (pos >= s.size() || s[pos] != '"') { return std::nullopt; }
  ++pos;  // skip closing quote
  return result;
}

// Parse a single line for @envy directive
// Returns key and value if found, nullopt otherwise
std::optional<std::pair<std::string, std::string>> parse_directive_line(
    std::string_view line) {
  size_t pos{ 0 };

  pos = skip_whitespace(line, pos);

  // Must start with "--"
  if (pos + 2 > line.size() || line[pos] != '-' || line[pos + 1] != '-') {
    return std::nullopt;
  }
  pos += 2;

  pos = skip_whitespace(line, pos);

  // Must have "@envy"
  if (pos + 5 > line.size()) { return std::nullopt; }
  if (line.substr(pos, 5) != "@envy") { return std::nullopt; }
  pos += 5;

  if (pos >= line.size() || (line[pos] != ' ' && line[pos] != '\t')) {
    return std::nullopt;
  }
  pos = skip_whitespace(line, pos);

  auto const key{ parse_identifier(line, pos) };
  if (key.empty()) { return std::nullopt; }

  pos = skip_whitespace(line, pos);

  auto const value{ parse_quoted_value(line, pos) };
  if (!value) { return std::nullopt; }

  return std::make_pair(std::string{ key }, *value);
}

}  // namespace

envy_meta parse_envy_meta(std::string_view content) {
  envy_meta result;
  size_t line_start{ 0 };

  while (line_start < content.size()) {
    size_t const line_end{ content.find('\n', line_start) };
    auto const line{ content.substr(
        line_start,
        (line_end == std::string_view::npos ? content.size() : line_end) - line_start) };

    if (auto const directive{ parse_directive_line(line) }) {
      auto const &[key, value]{ *directive };
      if (key == "version") {
        result.version = value;
      } else if (key == "cache") {
        result.cache = value;
      } else if (key == "mirror") {
        result.mirror = value;
      } else if (key == "bin-dir") {
        result.bin_dir = value;
      }
    }

    if (line_end == std::string_view::npos) { break; }
    line_start = line_end + 1;
  }

  return result;
}

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

std::filesystem::path manifest::find_manifest_path(
    std::optional<std::filesystem::path> const &explicit_path) {
  if (explicit_path) {
    auto const path{ std::filesystem::absolute(*explicit_path) };
    if (!std::filesystem::exists(path)) {
      throw std::runtime_error("manifest not found: " + path.string());
    }
    return path;
  } else {
    if (auto const discovered{ discover() }) { return *discovered; }
    throw std::runtime_error("manifest not found (discovery failed)");
  }
}

std::unique_ptr<manifest> manifest::load(std::filesystem::path const &manifest_path) {
  tui::debug("Loading manifest from file: %s", manifest_path.string().c_str());
  return load(util_load_file(manifest_path), manifest_path);
}

std::unique_ptr<manifest> manifest::load(std::vector<unsigned char> const &content,
                                         std::filesystem::path const &manifest_path) {
  tui::debug("Loading manifest (%zu bytes)", content.size());
  // Ensure null-termination for Lua (create string with guaranteed null terminator)
  std::string const script{ reinterpret_cast<char const *>(content.data()),
                            content.size() };

  auto meta{ parse_envy_meta(script) };

  if (!meta.bin_dir) {
    throw std::runtime_error(
        "Manifest missing required '@envy bin-dir' directive.\n"
        "Add to manifest header, e.g.: -- @envy bin-dir \"tools\"");
  }

  auto state{ sol_util_make_lua_state() };
  lua_envy_install(*state);

  if (sol::protected_function_result const result{
          state->safe_script(script, sol::script_pass_on_error) };
      !result.valid()) {
    sol::error err = result;
    throw std::runtime_error(std::string("Failed to execute manifest script: ") +
                             err.what());
  }

  auto m{ std::make_unique<manifest>() };
  m->manifest_path = manifest_path;
  m->meta = std::move(meta);
  m->lua_ = std::move(state);  // Keep lua state alive for DEFAULT_SHELL access

  sol::object packages_obj = (*m->lua_)["PACKAGES"];
  if (!packages_obj.valid() || packages_obj.get_type() != sol::type::table) {
    throw std::runtime_error("Manifest must define 'PACKAGES' global as a table");
  }

  sol::table packages_table = packages_obj.as<sol::table>();

  for (size_t i{ 1 }; i <= packages_table.size(); ++i) {
    m->packages.push_back(pkg_cfg::parse(packages_table[i], manifest_path));
  }

  return m;
}

std::unique_ptr<manifest> manifest::load(char const *script,
                                         std::filesystem::path const &manifest_path) {
  tui::debug("Loading manifest from C string");
  return load(std::vector<unsigned char>(script, script + std::strlen(script)),
              manifest_path);
}

default_shell_cfg_t manifest::get_default_shell(lua_ctx_common const *ctx) const {
  if (!lua_) { return std::nullopt; }

  sol::object default_shell_obj{ (*lua_)["DEFAULT_SHELL"] };
  if (!default_shell_obj.valid()) { return std::nullopt; }

  // Helper to convert flat variant to nested variant structure
  auto const convert_parsed{ [](resolved_shell const &parsed) -> default_shell_value {
    return std::visit(match{ [](shell_choice c) -> default_shell_value { return c; },
                             [](custom_shell_file const &f) -> default_shell_value {
                               return custom_shell{ f };
                             },
                             [](custom_shell_inline const &i) -> default_shell_value {
                               return custom_shell{ i };
                             } },
                      parsed);
  } };

  if (default_shell_obj.is<sol::protected_function>()) {
    sol::protected_function default_shell_func{
      default_shell_obj.as<sol::protected_function>()
    };

    sol::table ctx_table{ lua_->create_table() };
    ctx_table["package"] = make_ctx_package(const_cast<lua_ctx_common *>(ctx));

    sol::protected_function_result result{ default_shell_func(ctx_table) };
    if (!result.valid()) {
      sol::error err = result;
      throw std::runtime_error("DEFAULT_SHELL function failed: " +
                               std::string{ err.what() });
    }

    return convert_parsed(
        parse_shell_config_from_lua(result.get<sol::object>(), "DEFAULT_SHELL function"));
  }

  return convert_parsed(parse_shell_config_from_lua(default_shell_obj, "DEFAULT_SHELL"));
}

}  // namespace envy

#include "manifest.h"

#include "bundle.h"
#include "engine.h"
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

std::optional<bool> parse_bool_value(std::string_view value) {
  if (value == "true") { return true; }
  if (value == "false") { return false; }
  return std::nullopt;
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

using bundle_alias_map = std::unordered_map<std::string, pkg_cfg::bundle_source>;
using bundle_pkg_map = std::unordered_map<std::string, pkg_cfg *>;

// Parse a single package entry that may reference a bundle
pkg_cfg *parse_package_entry(sol::object const &entry,
                             std::filesystem::path const &manifest_path,
                             bundle_alias_map const &bundles,
                             bundle_pkg_map const &custom_fetch_bundle_pkgs) {
  // For non-table entries (strings) or tables without bundle field, use standard parsing
  if (!entry.is<sol::table>()) { return pkg_cfg::parse(entry, manifest_path); }

  sol::table table{ entry.as<sol::table>() };

  // Check for bundle field
  sol::object bundle_obj{ table["bundle"] };
  if (!bundle_obj.valid() || bundle_obj.get_type() == sol::type::lua_nil) {
    // No bundle field - use standard pkg_cfg::parse
    return pkg_cfg::parse(entry, manifest_path);
  }

  // Has bundle field - need to handle bundle reference
  std::string const spec_identity{ [&] {
    auto opt{ sol_util_get_optional<std::string>(table, "spec", "Package") };
    if (!opt.has_value() || opt->empty()) {
      throw std::runtime_error("Package with 'bundle' field requires 'spec' field");
    }
    return std::move(*opt);
  }() };

  // Check for source field - can't have both source and bundle
  if (sol::object source_obj{ table["source"] };
      source_obj.valid() && source_obj.get_type() != sol::type::lua_nil) {
    throw std::runtime_error("Package cannot specify both 'source' and 'bundle' fields");
  }

  pkg_cfg::bundle_source const bundle_src{ [&]() -> pkg_cfg::bundle_source {
    if (bundle_obj.is<std::string>()) {
      std::string const &alias{ bundle_obj.as<std::string>() };
      auto it{ bundles.find(alias) };
      if (it == bundles.end()) {
        throw std::runtime_error("Bundle alias '" + alias +
                                 "' not found in BUNDLES table for spec '" +
                                 spec_identity + "'");
      }
      return it->second;
    }
    if (bundle_obj.is<sol::table>()) {
      return bundle::parse_inline(bundle_obj.as<sol::table>(), manifest_path);
    }
    throw std::runtime_error("Package 'bundle' field must be string (alias) or table");
  }() };

  // Parse optional fields
  std::string serialized_options{ "{}" };
  sol::object options_obj{ table["options"] };
  if (options_obj.valid() && options_obj.get_type() == sol::type::table) {
    serialized_options = pkg_cfg::serialize_option_table(options_obj);
  }

  std::optional<pkg_phase> needed_by;
  auto needed_by_str{ sol_util_get_optional<std::string>(table, "needed_by", "Package") };
  if (needed_by_str.has_value()) {
    std::string const &nb{ *needed_by_str };
    if (nb == "check") {
      needed_by = pkg_phase::pkg_check;
    } else if (nb == "fetch") {
      needed_by = pkg_phase::pkg_fetch;
    } else if (nb == "stage") {
      needed_by = pkg_phase::pkg_stage;
    } else if (nb == "build") {
      needed_by = pkg_phase::pkg_build;
    } else if (nb == "install") {
      needed_by = pkg_phase::pkg_install;
    } else {
      throw std::runtime_error(
          "Package 'needed_by' must be one of: check, fetch, stage, build, install "
          "(got: " +
          nb + ")");
    }
  }

  std::optional<std::string> product{
    sol_util_get_optional<std::string>(table, "product", "Package")
  };

  std::string const bundle_identity{ bundle_src.bundle_identity };

  // Check if this bundle has custom fetch - if so, add implicit dependency on bundle pkg
  std::vector<pkg_cfg *> source_deps;
  if (auto it{ custom_fetch_bundle_pkgs.find(bundle_identity) };
      it != custom_fetch_bundle_pkgs.end()) {
    source_deps.push_back(it->second);
  }

  // Create pkg_cfg with bundle source
  pkg_cfg *cfg{ pkg_cfg::pool()->emplace(spec_identity,
                                         std::move(bundle_src),
                                         std::move(serialized_options),
                                         needed_by,
                                         nullptr,  // parent
                                         nullptr,  // weak
                                         std::move(source_deps),
                                         std::move(product),
                                         manifest_path) };

  // Set bundle-related fields
  cfg->bundle_identity = bundle_identity;
  // bundle_path will be resolved later when the bundle is fetched and parsed

  return cfg;
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
#ifdef _WIN32
      } else if (key == "cache-win") {
#else
      } else if (key == "cache-posix") {
#endif
        result.cache = value;
      } else if (key == "mirror") {
        result.mirror = value;
      } else if (key == "bin" || key == "bin-dir") {
        result.bin = value;
      } else if (key == "deploy") {
        result.deploy = parse_bool_value(value);
      } else if (key == "root") {
        result.root = parse_bool_value(value);
      }
    }

    if (line_end == std::string_view::npos) { break; }
    line_start = line_end + 1;
  }

  return result;
}

std::optional<std::filesystem::path> manifest::discover(
    bool nearest,
    std::filesystem::path const &start_dir) {
  namespace fs = std::filesystem;

  std::vector<fs::path> candidates;  // non-root manifests encountered during search
  auto cur{ start_dir };

  for (;;) {
    auto const manifest_path{ cur / "envy.lua" };
    if (fs::exists(manifest_path)) {
      // In nearest (subproject) mode, return the first envy.lua found
      if (nearest) { return manifest_path; }

      // Parse meta to check root directive
      auto content{ util_load_file(manifest_path) };
      auto meta{ parse_envy_meta(
          { reinterpret_cast<char const *>(content.data()), content.size() }) };

      // Default root=true (stops search); root=false continues upward
      bool const is_root{ !meta.root.has_value() || *meta.root };

      if (is_root) { return manifest_path; }
      // Non-root manifest: remember and continue searching
      candidates.push_back(manifest_path);
    }

    auto const git_path{ cur / ".git" };
    if (fs::exists(git_path) && fs::is_directory(git_path)) {
      // Hit .git boundary; use closest-to-root candidate if any
      return candidates.empty() ? std::nullopt
                                : std::optional<fs::path>{ candidates.back() };
    }

    auto const parent{ cur.parent_path() };
    if (parent == cur) {
      // Hit filesystem root; use closest-to-root candidate if any
      return candidates.empty() ? std::nullopt
                                : std::optional<fs::path>{ candidates.back() };
    }

    cur = parent;
  }
}

std::filesystem::path manifest::find_manifest_path(
    std::optional<std::filesystem::path> const &explicit_path,
    bool nearest) {
  if (explicit_path) {
    auto const path{ std::filesystem::absolute(*explicit_path) };
    if (!std::filesystem::exists(path)) {
      throw std::runtime_error("manifest not found: " + path.string());
    }
    return path;
  } else {
    if (auto const discovered{ discover(nearest, std::filesystem::current_path()) }) {
      return *discovered;
    }
    throw std::runtime_error("manifest not found (discovery failed)");
  }
}

std::unique_ptr<manifest> manifest::find_and_load(
    std::optional<std::filesystem::path> const &explicit_path,
    bool nearest) {
  return load(find_manifest_path(explicit_path, nearest));
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

  if (!meta.bin) {
    throw std::runtime_error(
        "Manifest missing required '@envy bin' directive.\n"
        "Add to manifest header, e.g.: -- @envy bin \"tools\"");
  }

  auto state{ sol_util_make_lua_state() };
  lua_envy_install(*state);

  // Use manifest path as chunk name so debug.getinfo can find it for envy.loadenv()
  std::string const chunk_name{ "@" + manifest_path.string() };
  if (sol::protected_function_result const result{
          state->safe_script(script, sol::script_pass_on_error, chunk_name) };
      !result.valid()) {
    sol::error err = result;
    throw std::runtime_error(std::string("Failed to execute manifest script: ") +
                             err.what());
  }

  auto m{ std::make_unique<manifest>() };
  m->manifest_path = manifest_path;
  m->meta = std::move(meta);
  m->lua_ = std::move(state);  // Keep lua state alive for DEFAULT_SHELL access

  auto const bundles{ bundle::parse_aliases((*m->lua_)["BUNDLES"], manifest_path) };

  // Create pkg_cfg entries for bundles with custom fetch (they become BUNDLE_ONLY
  // packages) Map of bundle_identity -> pkg_cfg* for packages to depend on
  std::unordered_map<std::string, pkg_cfg *> custom_fetch_bundle_pkgs;
  for (auto const &[alias, bundle_src] : bundles) {
    auto const *custom_fetch{ std::get_if<pkg_cfg::custom_fetch_source>(
        &bundle_src.fetch_source) };
    if (!custom_fetch) { continue; }

    // Create pkg_cfg for the bundle package
    pkg_cfg *bundle_cfg{ pkg_cfg::pool()->emplace(
        bundle_src.bundle_identity,  // identity = bundle identity
        pkg_cfg::bundle_source{ bundle_src },
        "{}",
        std::nullopt,                // needed_by (root package)
        nullptr,                     // parent
        nullptr,                     // weak
        custom_fetch->dependencies,  // source_dependencies
        std::nullopt,                // product
        manifest_path) };

    bundle_cfg->bundle_identity = bundle_src.bundle_identity;
    custom_fetch_bundle_pkgs[bundle_src.bundle_identity] = bundle_cfg;
    m->packages.push_back(bundle_cfg);
  }

  sol::object packages_obj = (*m->lua_)["PACKAGES"];
  if (!packages_obj.valid() || packages_obj.get_type() != sol::type::table) {
    throw std::runtime_error("Manifest must define 'PACKAGES' global as a table");
  }

  sol::table packages_table = packages_obj.as<sol::table>();

  for (size_t i{ 1 }; i <= packages_table.size(); ++i) {
    m->packages.push_back(parse_package_entry(packages_table[i],
                                              manifest_path,
                                              bundles,
                                              custom_fetch_bundle_pkgs));
  }

  return m;
}

std::unique_ptr<manifest> manifest::load(char const *script,
                                         std::filesystem::path const &manifest_path) {
  tui::debug("Loading manifest from C string");
  return load(std::vector<unsigned char>(script, script + std::strlen(script)),
              manifest_path);
}

default_shell_cfg_t manifest::get_default_shell() const {
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

    // DEFAULT_SHELL functions can use envy.package() directly via phase context
    sol::protected_function_result result{ default_shell_func() };
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

std::optional<std::string> manifest::run_bundle_fetch(
    std::string const &bundle_identity,
    void *phase_ctx,
    std::filesystem::path const &tmp_dir) const {
  std::lock_guard const lock(lua_mutex_);

  if (!lua_) { return "manifest Lua state unavailable"; }

  sol::object bundles_obj{ (*lua_)["BUNDLES"] };
  if (!bundles_obj.valid() || !bundles_obj.is<sol::table>()) {
    return "BUNDLES table not found";
  }

  sol::table bundles_table{ bundles_obj.as<sol::table>() };
  sol::protected_function fetch_func;
  bool found{ false };

  for (auto const &[key, value] : bundles_table) {
    if (!value.is<sol::table>()) { continue; }

    sol::table bundle_entry{ value.as<sol::table>() };

    sol::object identity_obj{ bundle_entry["identity"] };
    if (!identity_obj.valid() || !identity_obj.is<std::string>()) { continue; }
    if (identity_obj.as<std::string>() != bundle_identity) { continue; }

    sol::object source_obj{ bundle_entry["source"] };
    if (!source_obj.valid() || !source_obj.is<sol::table>()) { continue; }

    sol::table source_table{ source_obj.as<sol::table>() };
    sol::object fetch_obj{ source_table["fetch"] };
    if (!fetch_obj.valid() || !fetch_obj.is<sol::function>()) { continue; }

    fetch_func = fetch_obj.as<sol::protected_function>();
    found = true;
    break;
  }

  if (!found) { return "bundle fetch function not found: " + bundle_identity; }

  // RAII guard to clear registry on scope exit (including exceptions)
  sol::state_view lua_view{ *lua_ };
  struct registry_guard {
    sol::state_view &lua;
    ~registry_guard() { lua.registry()[ENVY_PHASE_CTX_RIDX] = sol::lua_nil; }
  } guard{ lua_view };

  lua_view.registry()[ENVY_PHASE_CTX_RIDX] = phase_ctx;

  sol::protected_function_result result{ fetch_func(tmp_dir.string()) };
  if (!result.valid()) {
    sol::error err = result;
    return std::string(err.what());
  }

  return std::nullopt;
}

}  // namespace envy

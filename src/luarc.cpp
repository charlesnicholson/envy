#include "luarc.h"

#include "bootstrap.h"
#include "embedded_init_resources.h"
#include "platform.h"
#include "tui.h"
#include "util.h"

#include "lua.h"
#include "picojson.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>

#ifndef ENVY_VERSION_STR
#error "ENVY_VERSION_STR must be defined by the build system"
#endif

namespace envy {

namespace fs = std::filesystem;

std::string make_portable_path(fs::path const &path) {
#ifdef _WIN32
  char const *home{ std::getenv("USERPROFILE") };
  char const env_var[]{ "${env:USERPROFILE}" };
  char const sep{ '\\' };
#else
  char const *home{ std::getenv("HOME") };
  char const env_var[]{ "${env:HOME}" };
  char const sep{ '/' };
#endif
  if (!home) {
    std::string result{ path.string() };
    std::replace(result.begin(), result.end(), '\\', '/');
    return result;
  }

  std::string const path_str{ path.string() };
  std::string const home_str{ home };

  if (path_str == home_str) { return env_var; }

  if (path_str.starts_with(home_str + sep)) {
    std::string result{ env_var + path_str.substr(home_str.size()) };
    std::replace(result.begin(), result.end(), '\\', '/');
    return result;
  }

  std::string result{ path_str };
  std::replace(result.begin(), result.end(), '\\', '/');
  return result;
}

namespace {

std::string_view get_type_definitions() {
  return { reinterpret_cast<char const *>(embedded::kTypeDefinitions),
           embedded::kTypeDefinitionsSize };
}

std::string_view get_luarc_template() {
  return { reinterpret_cast<char const *>(embedded::kLuarcTemplate),
           embedded::kLuarcTemplateSize };
}

void replace_all(std::string &s, std::string_view from, std::string_view to) {
  size_t pos{ 0 };
  while ((pos = s.find(from, pos)) != std::string::npos) {
    s.replace(pos, from.length(), to);
    pos += to.length();
  }
}

std::string stamp_placeholders(std::string_view content, std::string_view download_url) {
  std::string result{ content };
  replace_all(result, "@@ENVY_VERSION@@", ENVY_VERSION_STR);
  replace_all(result, "@@DOWNLOAD_URL@@", download_url);
  return result;
}

}  // namespace

std::optional<std::string> rewrite_luarc_types_path(std::string_view content,
                                                    std::string_view expected_path) {
  picojson::value root;
  std::string const json_str{ content };
  std::string err{ picojson::parse(root, json_str) };
  if (!err.empty() || !root.is<picojson::object>()) { return std::nullopt; }

  auto &obj{ root.get<picojson::object>() };
  auto it{ obj.find("workspace.library") };
  if (it == obj.end() || !it->second.is<picojson::array>()) { return std::nullopt; }

  auto &library{ it->second.get<picojson::array>() };

  // Derive prefix: expected is "<cache>/envy/<version>", prefix is "<cache>/envy/"
  auto const last_slash{ expected_path.rfind('/') };
  if (last_slash == std::string_view::npos) { return std::nullopt; }
  std::string_view const prefix{ expected_path.substr(0, last_slash + 1) };

  // Find the envy entry by matching the cache prefix
  int envy_idx{ -1 };
  for (int i{ 0 }; i < static_cast<int>(library.size()); ++i) {
    if (!library[i].is<std::string>()) { continue; }
    auto const &s{ library[i].get<std::string>() };
    if (s.starts_with(prefix)) {
      envy_idx = i;
      break;
    }
  }

  if (envy_idx < 0) {
    library.push_back(picojson::value(std::string{ expected_path }));
    return root.serialize(true);
  }

  auto const &current{ library[envy_idx].get<std::string>() };
  if (current == expected_path) { return std::nullopt; }

  library[envy_idx] = picojson::value(std::string{ expected_path });
  return root.serialize(true);
}

void update_luarc_types_path(fs::path const &project_dir, fs::path const &cache_root) {
  fs::path const luarc_path{ project_dir / ".luarc.json" };
  if (!fs::exists(luarc_path)) { return; }

  std::string const expected{ make_portable_path(cache_root / "envy" / ENVY_VERSION_STR) };

  auto const bytes{ util_load_file(luarc_path) };
  std::string const content{ bytes.begin(), bytes.end() };

  auto result{ rewrite_luarc_types_path(content, expected) };
  if (!result) { return; }

  util_write_file(luarc_path, *result);
  tui::info("Updated .luarc.json types path to %s", expected.c_str());
}

fs::path extract_lua_ls_types() {
  auto const cache_root{ platform::get_default_cache_root() };
  if (!cache_root) { throw std::runtime_error("init: failed to determine cache root"); }

  fs::path const types_dir{ *cache_root / "envy" / ENVY_VERSION_STR };
  fs::path const types_path{ types_dir / "envy.lua" };

  if (fs::exists(types_path)) { return types_dir; }

  std::error_code ec;
  fs::create_directories(types_dir, ec);
  if (ec) {
    throw std::runtime_error("init: failed to create types directory " +
                             types_dir.string() + ": " + ec.message());
  }

  auto const types{ stamp_placeholders(get_type_definitions(), kEnvyDownloadUrl) };
  util_write_file(types_path, types);

  tui::info("Extracted type definitions to %s", types_path.string().c_str());
  return types_dir;
}

void write_luarc(fs::path const &project_dir, fs::path const &types_dir) {
  fs::path const luarc_path{ project_dir / ".luarc.json" };

  std::string const portable_types_dir{ make_portable_path(types_dir) };

  if (fs::exists(luarc_path)) {
    tui::info("");
    tui::info(".luarc.json already exists at %s", luarc_path.string().c_str());
    tui::info("To enable envy autocompletion, add the following to workspace.library:");
    tui::info("  \"%s\"", portable_types_dir.c_str());
    return;
  }

  std::string content{ get_luarc_template() };
  replace_all(content, "@@LUA_VERSION@@", LUA_VERSION);
  replace_all(content, "@@TYPES_DIR@@", portable_types_dir);

  util_write_file(luarc_path, content);

  tui::info("Created %s", luarc_path.string().c_str());
}

}  // namespace envy

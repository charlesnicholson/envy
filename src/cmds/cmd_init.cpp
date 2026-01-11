#include "cmd_init.h"

#include "cache.h"
#include "embedded_init_resources.h"  // Generated from cmake/EmbedResource.cmake
#include "platform.h"
#include "tui.h"

#include "CLI11.hpp"
#include "lua.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>

#ifndef ENVY_VERSION_STR
#error "ENVY_VERSION_STR must be defined by the build system"
#endif

namespace envy {

namespace fs = std::filesystem;

constexpr std::string_view kEnvyDownloadUrl{
  "https://github.com/charlesnicholson/envy/releases/download"
};

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

void cmd_init::register_cli(CLI::App &app, std::function<void(cfg)> on_selected) {
  auto *sub{ app.add_subcommand("init",
                                "Initialize envy project with bootstrap scripts") };
  auto cfg_ptr{ std::make_shared<cfg>() };
  sub->add_option("project-dir", cfg_ptr->project_dir, "Project directory for manifest")
      ->required();
  sub->add_option("bin-dir", cfg_ptr->bin_dir, "Directory for bootstrap scripts")
      ->required();
  sub->add_option("--mirror", cfg_ptr->mirror, "Override download mirror URL");
  sub->add_option("--deploy", cfg_ptr->deploy, "Set @envy deploy directive (true/false)");
  sub->add_option("--root", cfg_ptr->root, "Set @envy root directive (true/false)");
  sub->callback(
      [cfg_ptr, on_selected = std::move(on_selected)] { on_selected(*cfg_ptr); });
}

namespace {

std::string_view get_bootstrap() {
  return { reinterpret_cast<char const *>(embedded::kBootstrap),
           embedded::kBootstrapSize };
}

std::string_view get_manifest_template() {
  return { reinterpret_cast<char const *>(embedded::kManifestTemplate),
           embedded::kManifestTemplateSize };
}

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

void write_file(fs::path const &path, std::string_view content) {
  std::ofstream out{ path, std::ios::binary };
  if (!out) { throw std::runtime_error("init: failed to create " + path.string()); }
  out.write(content.data(), static_cast<std::streamsize>(content.size()));
  if (!out.good()) { throw std::runtime_error("init: failed to write " + path.string()); }
}

std::string stamp_manifest_placeholders(std::string_view content,
                                        std::string_view download_url,
                                        std::string_view bin_dir,
                                        std::optional<bool> deploy,
                                        std::optional<bool> root) {
  std::string result{ content };
  replace_all(result, "@@ENVY_VERSION@@", ENVY_VERSION_STR);
  replace_all(result, "@@DOWNLOAD_URL@@", download_url);
  replace_all(result, "@@BIN_DIR@@", bin_dir);

  // Add deploy directive if specified
  if (deploy.has_value()) {
    replace_all(result,
                "@@DEPLOY_DIRECTIVE@@",
                *deploy ? "-- @envy deploy \"true\"\n" : "-- @envy deploy \"false\"\n");
  } else {
    replace_all(result, "@@DEPLOY_DIRECTIVE@@", "");
  }

  // Add root directive if specified
  if (root.has_value()) {
    replace_all(result,
                "@@ROOT_DIRECTIVE@@",
                *root ? "-- @envy root \"true\"\n" : "-- @envy root \"false\"\n");
  } else {
    replace_all(result, "@@ROOT_DIRECTIVE@@", "");
  }

  return result;
}

std::string stamp_placeholders(std::string_view content, std::string_view download_url) {
  std::string result{ content };
  replace_all(result, "@@ENVY_VERSION@@", ENVY_VERSION_STR);
  replace_all(result, "@@DOWNLOAD_URL@@", download_url);
  return result;
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
  write_file(types_path, types);

  tui::info("Extracted type definitions to %s", types_path.string().c_str());
  return types_dir;
}

void write_bootstrap(fs::path const &bin_dir, std::optional<std::string> const &mirror) {
#ifdef _WIN32
  fs::path const script_path{ bin_dir / "envy.bat" };
#else
  fs::path const script_path{ bin_dir / "envy" };
#endif

  std::string_view const url{ mirror ? std::string_view{ *mirror } : kEnvyDownloadUrl };
  std::string const content{ stamp_placeholders(get_bootstrap(), url) };
  write_file(script_path, content);

#ifndef _WIN32
  std::error_code ec;
  fs::permissions(script_path,
                  fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                  fs::perm_options::add,
                  ec);
  if (ec) {
    tui::warn("Failed to set executable bit on %s: %s",
              script_path.string().c_str(),
              ec.message().c_str());
  }
#endif

  tui::info("Created %s", script_path.string().c_str());
}

void write_manifest(fs::path const &project_dir,
                    fs::path const &bin_dir,
                    std::optional<bool> deploy,
                    std::optional<bool> root) {
  fs::path const manifest_path{ project_dir / "envy.lua" };

  if (fs::exists(manifest_path)) {
    tui::info("Manifest already exists: %s", manifest_path.string().c_str());
    return;
  }

  // Compute relative path from project_dir to bin_dir
  auto const abs_project{ fs::absolute(project_dir) };
  auto const abs_bin{ fs::absolute(bin_dir) };
  auto const relative_bin{ fs::relative(abs_bin, abs_project) };

  std::string const content{ stamp_manifest_placeholders(get_manifest_template(),
                                                         kEnvyDownloadUrl,
                                                         relative_bin.string(),
                                                         deploy,
                                                         root) };
  write_file(manifest_path, content);

  tui::info("Created %s", manifest_path.string().c_str());
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

  write_file(luarc_path, content);

  tui::info("Created %s", luarc_path.string().c_str());
}

}  // namespace

cmd_init::cmd_init(cmd_init::cfg cfg,
                   std::optional<std::filesystem::path> const &cli_cache_root)
    : cfg_{ std::move(cfg) }, cli_cache_root_{ cli_cache_root } {}

void cmd_init::execute() {
  auto c{ cache::ensure(cli_cache_root_, std::nullopt) };
  std::error_code ec;

  if (!fs::exists(cfg_.project_dir)) {
    fs::create_directories(cfg_.project_dir, ec);
    if (ec) {
      throw std::runtime_error("init: failed to create project directory " +
                               cfg_.project_dir.string() + ": " + ec.message());
    }
  }

  if (!fs::exists(cfg_.bin_dir)) {
    fs::create_directories(cfg_.bin_dir, ec);
    if (ec) {
      throw std::runtime_error("init: failed to create bin directory " +
                               cfg_.bin_dir.string() + ": " + ec.message());
    }
  }

  write_bootstrap(cfg_.bin_dir, cfg_.mirror);
  write_manifest(cfg_.project_dir, cfg_.bin_dir, cfg_.deploy, cfg_.root);
  write_luarc(cfg_.project_dir, extract_lua_ls_types());

  tui::info("");
  tui::info("Initialized envy project.");
  tui::info("Next steps:");
  tui::info("  1. Edit %s to add packages",
            (cfg_.project_dir / "envy.lua").string().c_str());
#ifdef _WIN32
  tui::info("  2. Run %s sync", (cfg_.bin_dir / "envy.bat").string().c_str());
#else
  tui::info("  2. Run %s sync", (cfg_.bin_dir / "envy").string().c_str());
#endif
}

}  // namespace envy

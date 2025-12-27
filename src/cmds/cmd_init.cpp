#include "cmd_init.h"

#include "bootstrap.h"
#include "cache.h"
#include "embedded_init_resources.h"  // Generated from cmake/EmbedResource.cmake
#include "tui.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#ifndef ENVY_VERSION_STR
#error "ENVY_VERSION_STR must be defined by the build system"
#endif

namespace envy {

namespace fs = std::filesystem;

namespace {

std::string_view get_bootstrap() {
  return { reinterpret_cast<char const *>(embedded::kBootstrap),
           embedded::kBootstrapSize };
}

std::string_view get_manifest_template() {
  return { reinterpret_cast<char const *>(embedded::kManifestTemplate),
           embedded::kManifestTemplateSize };
}

void write_file(fs::path const &path, std::string_view content) {
  std::ofstream out{ path, std::ios::binary };
  if (!out) { throw std::runtime_error("init: failed to create " + path.string()); }
  out.write(content.data(), static_cast<std::streamsize>(content.size()));
  if (!out.good()) { throw std::runtime_error("init: failed to write " + path.string()); }
}

void write_bootstrap(fs::path const &bin_dir, std::optional<std::string> const &mirror) {
#ifdef _WIN32
  fs::path const script_path{ bin_dir / "envy.bat" };
#else
  fs::path const script_path{ bin_dir / "envy" };
#endif

  std::string_view const url{ mirror ? std::string_view{ *mirror } : kEnvyDownloadUrl };
  std::string const content{ bootstrap_stamp_placeholders(get_bootstrap(), url) };
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

void write_manifest(fs::path const &project_dir) {
  fs::path const manifest_path{ project_dir / "envy.lua" };

  if (fs::exists(manifest_path)) {
    tui::info("Manifest already exists: %s", manifest_path.string().c_str());
    return;
  }

  std::string const content{ bootstrap_stamp_placeholders(get_manifest_template(),
                                                          kEnvyDownloadUrl) };
  write_file(manifest_path, content);

  tui::info("Created %s", manifest_path.string().c_str());
}

void write_luarc(fs::path const &project_dir, fs::path const &types_dir) {
  fs::path const luarc_path{ project_dir / ".luarc.json" };

  if (fs::exists(luarc_path)) {
    tui::info("");
    tui::info(".luarc.json already exists at %s", luarc_path.string().c_str());
    tui::info("To enable envy autocompletion, add the following to workspace.library:");
    tui::info("  \"%s\"", types_dir.string().c_str());
    return;
  }

  std::string const content{ R"({
  "$schema": "https://raw.githubusercontent.com/LuaLS/vscode-lua/master/setting/schema.json",
  "runtime.version": "Lua 5.4",
  "workspace.library": [
    ")" + types_dir.string() +
                             R"("
  ],
  "diagnostics.globals": [
    "envy", "IDENTITY", "PACKAGES", "DEPENDENCIES", "PRODUCTS",
    "FETCH", "STAGE", "BUILD", "INSTALL", "CHECK"
  ]
}
)" };

  write_file(luarc_path, content);

  tui::info("Created %s", luarc_path.string().c_str());
}

}  // namespace

cmd_init::cmd_init(cmd_init::cfg cfg, cache &c) : cfg_{ std::move(cfg) }, cache_{ c } {}

void cmd_init::execute() {
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
  write_manifest(cfg_.project_dir);

  fs::path const types_dir{ bootstrap_extract_lua_ls_types() };
  write_luarc(cfg_.project_dir, types_dir);

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

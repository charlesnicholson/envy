#include "cmd_init.h"

#include "bootstrap.h"
#include "embedded_init_resources.h"  // Generated from cmake/EmbedResource.cmake
#include "luarc.h"
#include "platform.h"
#include "self_deploy.h"
#include "tui.h"
#include "util.h"

#include "CLI11.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#ifndef ENVY_VERSION_STR
#error "ENVY_VERSION_STR must be defined by the build system"
#endif

namespace envy {

namespace fs = std::filesystem;

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
  sub->add_option("--platform",
                  cfg_ptr->platform_flag,
                  "Script platform: posix, windows, or all (default: current OS)")
      ->check(CLI::IsMember({ "posix", "windows", "all" }));
  sub->callback(
      [cfg_ptr, on_selected = std::move(on_selected)] { on_selected(*cfg_ptr); });
}

namespace {

std::string_view get_manifest_template() {
  return { reinterpret_cast<char const *>(embedded::kManifestTemplate),
           embedded::kManifestTemplateSize };
}

void replace_all(std::string &s, std::string_view from, std::string_view to) {
  size_t pos{ 0 };
  while ((pos = s.find(from, pos)) != std::string::npos) {
    s.replace(pos, from.length(), to);
    pos += to.length();
  }
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
  util_write_file(manifest_path, content);

  tui::info("Created %s", manifest_path.string().c_str());
}

}  // namespace

cmd_init::cmd_init(cmd_init::cfg cfg,
                   std::optional<std::filesystem::path> const &cli_cache_root)
    : cfg_{ std::move(cfg) }, cli_cache_root_{ cli_cache_root } {}

void cmd_init::execute() {
  auto c{ self_deploy::ensure(cli_cache_root_, std::nullopt) };
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

  auto const platforms{ util_parse_platform_flag(cfg_.platform_flag) };
  for (auto const plat : platforms) {
    bootstrap_write_script(cfg_.bin_dir, cfg_.mirror, plat);
    auto const name{ (plat == platform_id::WINDOWS) ? "envy.bat" : "envy" };
    tui::info("Created %s", (cfg_.bin_dir / name).string().c_str());
  }

  write_manifest(cfg_.project_dir, cfg_.bin_dir, cfg_.deploy, cfg_.root);
  write_luarc(cfg_.project_dir, extract_lua_ls_types());

  tui::info("");
  tui::info("Initialized envy project.");
  tui::info("Next steps:");
  tui::info("  1. Edit %s to add packages",
            (cfg_.project_dir / "envy.lua").string().c_str());
  auto const native_name{ (platform::native() == platform_id::WINDOWS) ? "envy.bat"
                                                                       : "envy" };
  tui::info("  2. Run %s sync", (cfg_.bin_dir / native_name).string().c_str());
}

}  // namespace envy

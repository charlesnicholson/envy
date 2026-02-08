#include "bootstrap.h"

#include "embedded_init_resources.h"
#include "tui.h"
#include "util.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

#ifndef ENVY_VERSION_STR
#error "ENVY_VERSION_STR must be defined by the build system"
#endif

namespace envy {

namespace fs = std::filesystem;

namespace {

constexpr std::string_view kEnvyDownloadUrl{
  "https://github.com/charlesnicholson/envy/releases/download"
};

constexpr std::string_view kEnvyManagedMarker{ "envy-managed" };

std::string_view get_bootstrap_template() {
  return { reinterpret_cast<char const *>(embedded::kBootstrap),
           embedded::kBootstrapSize };
}

void replace_all(std::string &s, std::string_view from, std::string_view to) {
  size_t pos{ 0 };
  while ((pos = s.find(from, pos)) != std::string::npos) {
    s.replace(pos, from.length(), to);
    pos += to.length();
  }
}

std::string stamp_bootstrap(std::string_view download_url) {
  std::string result{ get_bootstrap_template() };
  replace_all(result, "@@ENVY_VERSION@@", ENVY_VERSION_STR);
  replace_all(result, "@@DOWNLOAD_URL@@", download_url);
  return result;
}

std::string read_file_content(fs::path const &path) {
  if (!fs::exists(path)) { return {}; }
  std::ifstream in{ path, std::ios::binary };
  if (!in) { return {}; }
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}


fs::path bootstrap_script_path(fs::path const &bin_dir) {
#ifdef _WIN32
  return bin_dir / "envy.bat";
#else
  return bin_dir / "envy";
#endif
}

void set_executable(fs::path const &path) {
#ifndef _WIN32
  std::error_code ec;
  fs::permissions(path,
                  fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                  fs::perm_options::add,
                  ec);
  if (ec) {
    tui::warn("Failed to set executable bit on %s: %s",
              path.string().c_str(),
              ec.message().c_str());
  }
#else
  (void)path;
#endif
}

}  // namespace

bool bootstrap_is_envy_managed(fs::path const &path) {
  std::string const content{ read_file_content(path) };
  return content.find(kEnvyManagedMarker) != std::string::npos;
}

bool bootstrap_write_script(fs::path const &bin_dir,
                            std::optional<std::string> const &mirror) {
  fs::path const script_path{ bootstrap_script_path(bin_dir) };

  // Check if existing file is envy-managed
  if (fs::exists(script_path) && !bootstrap_is_envy_managed(script_path)) {
    throw std::runtime_error(
        "bootstrap: file '" + script_path.string() +
        "' exists but is not envy-managed. Remove manually to allow envy to manage it.");
  }

  // Generate new content
  std::string_view const url{ mirror ? std::string_view{ *mirror } : kEnvyDownloadUrl };
  std::string const new_content{ stamp_bootstrap(url) };

  // Compare with existing
  std::string const existing_content{ read_file_content(script_path) };
  if (new_content == existing_content) { return false; }

  // Write atomically
  util_write_file(script_path, new_content);
  set_executable(script_path);

  return true;
}

}  // namespace envy

#include "bootstrap.h"

#include "embedded_init_resources.h"
#include "platform.h"
#include "tui.h"

#include <fstream>
#include <string>
#include <string_view>

#ifndef ENVY_VERSION_STR
#error "ENVY_VERSION_STR must be defined by the build system"
#endif

namespace envy {

namespace fs = std::filesystem;

namespace {

std::string_view get_type_definitions() {
  return { reinterpret_cast<char const *>(embedded::kTypeDefinitions),
           embedded::kTypeDefinitionsSize };
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
  if (!out) { throw std::runtime_error("bootstrap: failed to create " + path.string()); }
  out.write(content.data(), static_cast<std::streamsize>(content.size()));
  if (!out.good()) {
    throw std::runtime_error("bootstrap: failed to write " + path.string());
  }
}

bool copy_binary(fs::path const &src, fs::path const &dst) {
  std::error_code ec;
  fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
  if (ec) {
    tui::warn("bootstrap: failed to copy binary: %s", ec.message().c_str());
    return false;
  }

#ifndef _WIN32
  fs::permissions(dst,
                  fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                  fs::perm_options::add,
                  ec);
  if (ec) {
    tui::warn("bootstrap: failed to set executable permissions: %s", ec.message().c_str());
  }
#endif

  return true;
}

}  // namespace

std::string bootstrap_stamp_placeholders(std::string_view content,
                                         std::string_view download_url) {
  std::string result{ content };
  replace_all(result, "@@ENVY_VERSION@@", ENVY_VERSION_STR);
  replace_all(result, "@@DOWNLOAD_URL@@", download_url);
  return result;
}

bool bootstrap_deploy_envy(cache &c) {
  auto const [envy_dir, needs_install]{ c.ensure_envy(ENVY_VERSION_STR) };
  if (!needs_install) { return true; }

#ifdef _WIN32
  fs::path const binary_path{ envy_dir / "envy.exe" };
#else
  fs::path const binary_path{ envy_dir / "envy" };
#endif
  fs::path const types_path{ envy_dir / "envy.lua" };

  fs::path const exe_path{ platform::get_exe_path() };
  if (!copy_binary(exe_path, binary_path)) { return true; }

  auto const types{ bootstrap_stamp_placeholders(get_type_definitions(),
                                                 kEnvyDownloadUrl) };
  write_file(types_path, types);

  return true;
}

std::filesystem::path bootstrap_extract_lua_ls_types() {
  auto const cache_root{ platform::get_default_cache_root() };
  if (!cache_root) {
    throw std::runtime_error("bootstrap: failed to determine cache root");
  }

  fs::path const types_dir{ *cache_root / "envy" / ENVY_VERSION_STR };
  fs::path const types_path{ types_dir / "envy.lua" };

  if (fs::exists(types_path)) { return types_dir; }

  std::error_code ec;
  fs::create_directories(types_dir, ec);
  if (ec) {
    throw std::runtime_error("bootstrap: failed to create types directory " +
                             types_dir.string() + ": " + ec.message());
  }

  auto const types{ bootstrap_stamp_placeholders(get_type_definitions(),
                                                 kEnvyDownloadUrl) };
  write_file(types_path, types);

  tui::info("Extracted type definitions to %s", types_path.string().c_str());
  return types_dir;
}

}  // namespace envy

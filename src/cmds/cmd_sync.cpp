#include "cmd_sync.h"

#include "cache.h"
#include "embedded_init_resources.h"
#include "engine.h"
#include "manifest.h"
#include "pkg_cfg.h"
#include "tui.h"

#include "CLI11.hpp"

#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>

#ifndef ENVY_VERSION_STR
#error "ENVY_VERSION_STR must be defined by the build system"
#endif

namespace envy {

namespace fs = std::filesystem;

void cmd_sync::register_cli(CLI::App &app, std::function<void(cfg)> on_selected) {
  auto *sub{
    app.add_subcommand("sync", "Deploy product scripts (or install with --install-all)")
  };
  auto cfg_ptr{ std::make_shared<cfg>() };
  sub->add_option("identities",
                  cfg_ptr->identities,
                  "Spec identities to sync (sync all if omitted)");
  sub->add_option("--manifest", cfg_ptr->manifest_path, "Path to envy.lua manifest");
  sub->add_flag("--install-all",
                cfg_ptr->install_all,
                "Install all packages (not just create product scripts)");
  sub->callback(
      [cfg_ptr, on_selected = std::move(on_selected)] { on_selected(*cfg_ptr); });
}

cmd_sync::cmd_sync(cfg cfg, std::optional<std::filesystem::path> const &cli_cache_root)
    : cfg_{ std::move(cfg) }, cli_cache_root_{ cli_cache_root } {}

namespace {

std::string_view get_product_script_template() {
  return { reinterpret_cast<char const *>(embedded::kProductScript),
           embedded::kProductScriptSize };
}

void replace_all(std::string &s, std::string_view from, std::string_view to) {
  size_t pos{ 0 };
  while ((pos = s.find(from, pos)) != std::string::npos) {
    s.replace(pos, from.length(), to);
    pos += to.length();
  }
}

std::string stamp_product_script(std::string_view product_name) {
  std::string result{ get_product_script_template() };
  replace_all(result, "@@ENVY_VERSION@@", ENVY_VERSION_STR);
  replace_all(result, "@@PRODUCT_NAME@@", product_name);
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

bool has_envy_marker(fs::path const &path) {
  std::string const content{ read_file_content(path) };
  return content.find("envy-managed") != std::string::npos;
}

void write_file_atomic(fs::path const &path, std::string_view content) {
  fs::path const temp_path{ path.parent_path() /
                            (".envy-tmp-" + path.filename().string()) };

  {
    std::ofstream out{ temp_path, std::ios::binary };
    if (!out) {
      throw std::runtime_error("sync: failed to create temp file " + temp_path.string());
    }
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!out.good()) {
      throw std::runtime_error("sync: failed to write temp file " + temp_path.string());
    }
  }

  std::error_code ec;
  fs::rename(temp_path, path, ec);
  if (ec) {
    fs::remove(temp_path, ec);
    throw std::runtime_error("sync: failed to rename " + temp_path.string() + " to " +
                             path.string() + ": " + ec.message());
  }
}

fs::path product_script_path(fs::path const &bin_dir, std::string_view product_name) {
#ifdef _WIN32
  return bin_dir / (std::string(product_name) + ".bat");
#else
  return bin_dir / product_name;
#endif
}

void deploy_product_scripts(engine &eng,
                            fs::path const &bin_dir,
                            std::vector<product_info> const &products) {
  std::set<std::string> current_products;
  for (auto const &p : products) { current_products.insert(p.product_name); }

  size_t created{ 0 };
  size_t updated{ 0 };
  size_t unchanged{ 0 };

  for (auto const &product : products) {
    fs::path const script_path{ product_script_path(bin_dir, product.product_name) };

    if (fs::exists(script_path) && !has_envy_marker(script_path)) {
      throw std::runtime_error(
          "sync: file '" + script_path.string() +
          "' exists but is not envy-managed. Remove manually or rename product.");
    }

    std::string const new_content{ stamp_product_script(product.product_name) };
    std::string const existing_content{ read_file_content(script_path) };
    if (new_content == existing_content) {
      ++unchanged;
      continue;
    }

    bool const is_new{ existing_content.empty() };
    write_file_atomic(script_path, new_content);

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

    if (is_new) {
      ++created;
      tui::debug("Created product script: %s", script_path.string().c_str());
    } else {
      ++updated;
      tui::debug("Updated product script: %s", script_path.string().c_str());
    }
  }

  size_t removed{ 0 };
  std::error_code ec;
  for (auto const &entry : fs::directory_iterator(bin_dir, ec)) {
    if (!entry.is_regular_file()) { continue; }

    std::string filename{ entry.path().filename().string() };
    if (filename == "envy" || filename == "envy.bat") { continue; }

    std::string product_name{ filename };
#ifdef _WIN32
    if (filename.size() > 4 && filename.substr(filename.size() - 4) == ".bat") {
      product_name = filename.substr(0, filename.size() - 4);
    }
#endif

    if (!current_products.contains(product_name) && has_envy_marker(entry.path())) {
      std::error_code rm_ec;
      fs::remove(entry.path(), rm_ec);
      if (rm_ec) {
        tui::warn("Failed to remove obsolete script %s: %s",
                  entry.path().string().c_str(),
                  rm_ec.message().c_str());
      } else {
        ++removed;
        tui::debug("Removed obsolete product script: %s", entry.path().string().c_str());
      }
    }
  }

  if (ec) {
    tui::warn("Failed to iterate bin directory %s: %s",
              bin_dir.string().c_str(),
              ec.message().c_str());
    return;
  }

  tui::info(
      "sync: %zu product script(s) (%zu created, %zu updated, %zu unchanged, %zu removed)",
      products.size(),
      created,
      updated,
      unchanged,
      removed);
}

}  // namespace

void cmd_sync::execute() {
  auto const m{ manifest::load(manifest::find_manifest_path(cfg_.manifest_path)) };
  if (!m) { throw std::runtime_error("sync: could not load manifest"); }

  if (!m->meta.bin) {
    throw std::runtime_error(
        "sync: manifest missing '@envy bin' directive (required for sync)");
  }

  auto c{ cache::ensure(cli_cache_root_, m->meta.cache) };

  fs::path const manifest_dir{ m->manifest_path.parent_path() };
  fs::path const bin_dir{ manifest_dir / *m->meta.bin };

  if (!fs::exists(bin_dir)) {
    std::error_code ec;
    fs::create_directories(bin_dir, ec);
    if (ec) {
      throw std::runtime_error("sync: failed to create bin directory " + bin_dir.string() +
                               ": " + ec.message());
    }
  }

  std::vector<pkg_cfg const *> targets;

  if (cfg_.identities.empty()) {
    for (auto const *pkg : m->packages) { targets.push_back(pkg); }
  } else {
    for (auto const &identity : cfg_.identities) {
      bool found{ false };
      for (auto const *pkg : m->packages) {
        if (pkg->identity == identity) {
          targets.push_back(pkg);
          found = true;
          break;
        }
      }
      if (!found) {
        throw std::runtime_error("sync: identity '" + identity +
                                 "' not found in manifest");
      }
    }
  }

  if (targets.empty()) {
    tui::info("nothing to sync");
    return;
  }

  engine eng{ *c, m.get() };

  if (cfg_.install_all) {
    auto result{ eng.run_full(targets) };

    size_t completed{ 0 };
    size_t user_managed{ 0 };
    size_t failed{ 0 };

    for (auto const &[key, outcome] : result) {
      if (outcome.type == pkg_type::UNKNOWN) {
        ++failed;
      } else if (outcome.type == pkg_type::USER_MANAGED) {
        ++user_managed;
      } else {
        ++completed;
      }
    }

    tui::info("sync: %zu package(s) installed, %zu user-managed, %zu failed",
              completed,
              user_managed,
              failed);

    if (failed > 0) {
      throw std::runtime_error("sync: " + std::to_string(failed) + " package(s) failed");
    }
  } else {
    eng.resolve_graph(targets);
  }

  auto const products{ eng.collect_all_products() };

  // Check deploy directive: absent or false means deployment disabled
  bool const deploy_enabled{ m->meta.deploy.has_value() && *m->meta.deploy };

  if (deploy_enabled) {
    deploy_product_scripts(eng, bin_dir, products);
  } else if (!cfg_.install_all) {
    // Naked sync with deploy disabled: warn user
    tui::warn("sync was requested but deployment is disabled in %s",
              m->manifest_path.string().c_str());
    tui::info("Add '-- @envy deploy \"true\"' to enable product script deployment");
  }
}

}  // namespace envy

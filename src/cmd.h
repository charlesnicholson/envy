#pragma once

#include "util.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>

namespace envy {

class cache;
struct manifest;

struct subprocess_exit {  // for commands that proxy subprocess exit codes
  int code;
};

// Shared command prologue: load manifest (throws "<cmd_name>: could not load
// manifest"), re-exec if the manifest pins a different envy version, then
// self-deploy into the cache. subproject enables nearest-manifest discovery.
struct cmd_startup {
  std::unique_ptr<manifest> m;
  std::unique_ptr<cache> c;
};

cmd_startup cmd_startup_load(std::string_view cmd_name,
                             std::optional<std::filesystem::path> const &manifest_path,
                             std::optional<std::filesystem::path> const &cli_cache_root,
                             bool subproject = false);

class cmd : unmovable {
 public:
  using ptr_t = std::unique_ptr<cmd>;

  virtual ~cmd() = default;
  virtual void execute() = 0;

  template <typename config>
  static ptr_t create(config const &cfg,
                      std::optional<std::filesystem::path> const &cli_cache_root);

 protected:
  cmd() = default;
};

// Command configs inherit from this for factory creation.
template <typename command>
struct cmd_cfg {
  using cmd_t = command;
};

template <typename config>
cmd::ptr_t cmd::create(config const &cfg,
                       std::optional<std::filesystem::path> const &cli_cache_root) {
  return std::make_unique<typename config::cmd_t>(cfg, cli_cache_root);
}

}  // namespace envy

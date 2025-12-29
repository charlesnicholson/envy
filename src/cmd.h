#pragma once

#include "util.h"

#include <filesystem>
#include <memory>
#include <optional>

namespace envy {

class cmd : unmovable {
 public:
  using ptr_t = std::unique_ptr<cmd>;

  virtual ~cmd() = default;
  virtual void execute() = 0;

  // Create command with CLI cache root override (for commands that may need cache)
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

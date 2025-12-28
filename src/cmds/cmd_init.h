#pragma once

#include "cache.h"
#include "cmd.h"

#include <filesystem>
#include <optional>
#include <string>

namespace envy {

// Convert absolute path to use $HOME for portability in .luarc.json
std::string make_portable_path(std::filesystem::path const &path);

class cmd_init : public cmd {
 public:
  struct cfg : cmd_cfg<cmd_init> {
    std::filesystem::path project_dir;
    std::filesystem::path bin_dir;
    std::optional<std::string> mirror;
  };

  cmd_init(cfg cfg, cache &c);

  void execute() override;
  cfg const &get_cfg() const { return cfg_; }

 private:
  cfg cfg_;
  cache &cache_;
};

}  // namespace envy

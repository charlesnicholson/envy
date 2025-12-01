#pragma once

#include "cmd.h"

#include <filesystem>
#include <optional>
#include <string>

namespace envy {

class cmd_product : public cmd {
 public:
  struct cfg : cmd_cfg<cmd_product> {
    std::string product_name;  // Required product key
    std::optional<std::filesystem::path> manifest_path;
    std::optional<std::filesystem::path> cache_root;
  };

  explicit cmd_product(cfg cfg);

  bool execute() override;
  cfg const &get_cfg() const { return cfg_; }

 private:
  cfg cfg_;
};

}  // namespace envy

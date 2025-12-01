#pragma once

#include "cmd.h"

#include <filesystem>
#include <optional>
#include <string>

namespace envy {

class cmd_product : public cmd {
 public:
  struct cfg : cmd_cfg<cmd_product> {
    std::string product_name;  // Optional: if empty, list all products
    std::optional<std::filesystem::path> manifest_path;
    std::optional<std::filesystem::path> cache_root;
    bool json{ false };  // JSON output mode
  };

  explicit cmd_product(cfg cfg);

  bool execute() override;
  cfg const &get_cfg() const { return cfg_; }

 private:
  cfg cfg_;
};

}  // namespace envy

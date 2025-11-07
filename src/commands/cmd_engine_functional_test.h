#pragma once

#include "cmd.h"

#include <filesystem>
#include <optional>
#include <string>

namespace envy {

class cmd_engine_functional_test : public cmd {
 public:
  struct cfg : cmd_cfg<cmd_engine_functional_test> {
    std::string identity;
    std::filesystem::path recipe_path;
    std::optional<std::filesystem::path> cache_root;
    int fail_after_fetch_count = -1;  // -1 = disabled, >0 = fail after N files
  };

  explicit cmd_engine_functional_test(cfg cfg);

  bool execute() override;
  cfg const &get_cfg() const { return cfg_; }

 private:
  cfg cfg_;
};

}  // namespace envy

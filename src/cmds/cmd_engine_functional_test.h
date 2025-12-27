#pragma once

#include "cmd.h"

#include <filesystem>
#include <string>

namespace envy {

class cmd_engine_functional_test : public cmd {
 public:
  struct cfg : cmd_cfg<cmd_engine_functional_test> {
    std::string identity;
    std::filesystem::path recipe_path;
    int fail_after_fetch_count = -1;  // -1 = disabled, >0 = fail after N files
  };

  cmd_engine_functional_test(cfg cfg, cache &c);

  void execute() override;
  cfg const &get_cfg() const { return cfg_; }

 private:
  cfg cfg_;
  cache &cache_;
};

}  // namespace envy

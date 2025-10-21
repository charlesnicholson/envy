#pragma once

#include "cmd.h"

#include "tbb/flow_graph.h"

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>

namespace envy {

class cmd_playground : public cmd {
 public:
  struct cfg : cmd_cfg<cmd_playground> {
    std::string s3_uri;
    std::string region;
  };

  explicit cmd_playground(cfg cfg);

  void schedule(tbb::flow::graph &g) override;
  cfg const &get_cfg() const { return cfg_; }

 private:
  cfg cfg_;

  std::mutex console_mutex_;
  std::filesystem::path workspace_root_;
  std::string git_probe_url_;
  std::string curl_probe_url_;
  std::string bucket_;
  std::string key_;

  std::optional<tbb::flow::broadcast_node<tbb::flow::continue_msg>> kickoff_;
  std::optional<tbb::flow::continue_node<tbb::flow::continue_msg>> lua_task_;
  std::optional<tbb::flow::continue_node<tbb::flow::continue_msg>> git_task_;
  std::optional<tbb::flow::continue_node<tbb::flow::continue_msg>> curl_task_;
};

}  // namespace envy

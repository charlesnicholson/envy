#pragma once

#include "command.h"

#include "tbb/flow_graph.h"

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>

namespace envy {

class playground_command : public command {
 public:
  struct config : command_cfg<playground_command> {
    std::string s3_uri;
    std::string region;
  };

  explicit playground_command(config cfg);

  void schedule(tbb::flow::graph &g) override;

 private:
  config config_;

  // State that must outlive schedule() until graph completes
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

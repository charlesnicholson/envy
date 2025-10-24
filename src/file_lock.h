#pragma once

#include "util.h"

#include <cstdint>
#include <filesystem>
#include <memory>

namespace envy {

class file_lock : unmovable {
 public:
  explicit file_lock(std::filesystem::path const &path);
  ~file_lock();

 private:
  std::intptr_t handle_;
};

using file_lock_ptr = std::unique_ptr<file_lock>;

}  // namespace envy

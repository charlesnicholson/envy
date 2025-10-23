#pragma once

#include "util.h"

#include <cstdint>
#include <filesystem>

namespace envy {

class file_lock : uncopyable {
 public:
  explicit file_lock(std::filesystem::path const &path);
  ~file_lock();

  file_lock(file_lock &&other) noexcept;
  file_lock &operator=(file_lock &&other) noexcept;

 private:
  std::intptr_t handle_;
};

}  // namespace envy

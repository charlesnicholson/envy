#pragma once

#include "util.h"

#include <cstdint>
#include <filesystem>
#include <memory>

namespace envy {

class file_lock : unmovable {
 public:
  using ptr_t = std::unique_ptr<file_lock>;

  static ptr_t make(std::filesystem::path const &path);
  ~file_lock();

 private:
  explicit file_lock(std::filesystem::path const &path);
  friend ptr_t std::make_unique<file_lock>(std::filesystem::path const &);

  std::intptr_t handle_;
};

}  // namespace envy

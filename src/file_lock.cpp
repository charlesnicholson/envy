#include "file_lock.h"

#include "platform.h"

#include <utility>

namespace envy {

file_lock::file_lock(std::filesystem::path const &path)
    : handle_{ platform::lock_file(path) } {}

file_lock::~file_lock() { platform::unlock_file(handle_); }

file_lock::file_lock(file_lock &&other) noexcept : handle_{ other.handle_ } {
  other.handle_ = platform::kInvalidLockHandle;
}

file_lock &file_lock::operator=(file_lock &&other) noexcept {
  if (this != &other) {
    platform::unlock_file(handle_);
    handle_ = other.handle_;
    other.handle_ = platform::kInvalidLockHandle;
  }
  return *this;
}

}  // namespace envy

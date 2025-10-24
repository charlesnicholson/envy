#include "file_lock.h"

#include "platform.h"

namespace envy {

file_lock::file_lock(std::filesystem::path const &path)
    : handle_{ platform::lock_file(path) } {}

file_lock::~file_lock() { platform::unlock_file(handle_); }

file_lock::ptr_t file_lock::make(std::filesystem::path const &path) {
  return std::make_unique<file_lock>(path);
}

}  // namespace envy

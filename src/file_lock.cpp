#include "file_lock.h"

#include "platform.h"

namespace envy {

file_lock::file_lock(std::filesystem::path const &path)
    : handle_{ platform::lock_file(path) } {}

file_lock::~file_lock() { platform::unlock_file(handle_); }

}  // namespace envy

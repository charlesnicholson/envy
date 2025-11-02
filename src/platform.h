#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>

namespace envy::platform {

using file_lock_handle_t = std::intptr_t;
extern file_lock_handle_t const kInvalidLockHandle;
file_lock_handle_t lock_file(std::filesystem::path const &path);
void unlock_file(file_lock_handle_t handle);

void atomic_rename(std::filesystem::path const &from, std::filesystem::path const &to);
void touch_file(std::filesystem::path const &path);

std::optional<std::filesystem::path> get_default_cache_root();
char const *get_default_cache_root_env_vars();

void set_env_var(char const *name, char const *value);

[[noreturn]] void terminate_process();

bool is_tty();

}  // namespace envy::platform

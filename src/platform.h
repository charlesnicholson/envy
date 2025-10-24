#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>

namespace envy::platform {

#ifdef _WIN32
constexpr std::intptr_t kInvalidLockHandle = 0;
#else
constexpr std::intptr_t kInvalidLockHandle = -1;
#endif

std::intptr_t lock_file(std::filesystem::path const &path);
void unlock_file(std::intptr_t handle);

void atomic_rename(std::filesystem::path const &from, std::filesystem::path const &to);

std::optional<std::filesystem::path> get_default_cache_root();
char const *get_default_cache_root_env_vars();

[[noreturn]] void terminate_process();

}  // namespace envy::platform

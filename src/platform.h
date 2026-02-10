#pragma once

#include "util.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>

// Platform-specific unreachable hint. Use compiler intrinsics where available
// while remaining safe for MSVC which lacks __builtin_unreachable.
#if defined(_MSC_VER)
#define ENVY_UNREACHABLE() __assume(0)
#else
#define ENVY_UNREACHABLE() __builtin_unreachable()
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <io.h>
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

namespace envy::platform {

class file_lock : uncopyable {
 public:
  explicit file_lock(std::filesystem::path const &path);
  ~file_lock();
  file_lock(file_lock &&) noexcept;
  file_lock &operator=(file_lock &&) noexcept;

  explicit operator bool() const;

 private:
  struct impl;
  std::unique_ptr<impl> impl_;
};

void atomic_rename(std::filesystem::path const &from, std::filesystem::path const &to);
void touch_file(std::filesystem::path const &path);
void flush_directory(std::filesystem::path const &dir);
bool file_exists(std::filesystem::path const &path);

std::optional<std::filesystem::path> get_default_cache_root();
char const *get_default_cache_root_env_vars();

std::filesystem::path get_exe_path();
std::filesystem::path expand_path(std::string_view path);

void set_env_var(char const *name, char const *value);

// Remove directory recursively with retry logic for Windows file locking issues.
// On Windows, antivirus/indexer may hold file handles briefly after creation.
// Returns default error_code on success, or the final OS error on failure.
std::error_code remove_all_with_retry(std::filesystem::path const &target);

[[noreturn]] void terminate_process();

bool is_tty();

std::string_view os_name();
std::string_view arch_name();

}  // namespace envy::platform

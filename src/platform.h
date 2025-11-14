#pragma once

#include <filesystem>
#include <memory>
#include <optional>

// Platform-specific unreachable hint. Use compiler intrinsics where available
// while remaining safe for MSVC which lacks __builtin_unreachable.
#if defined(_MSC_VER)
#define ENVY_UNREACHABLE() __assume(0)
#else
#define ENVY_UNREACHABLE() __builtin_unreachable()
#endif

namespace envy::platform {

class file_lock {
 public:
  explicit file_lock(std::filesystem::path const &path);
  ~file_lock();
  file_lock(file_lock &&) noexcept;
  file_lock &operator=(file_lock &&) noexcept;
  file_lock(file_lock const &) = delete;
  file_lock &operator=(file_lock const &) = delete;

  explicit operator bool() const;

 private:
  struct impl;
  std::unique_ptr<impl> impl_;
};

void atomic_rename(std::filesystem::path const &from, std::filesystem::path const &to);
void touch_file(std::filesystem::path const &path);

std::optional<std::filesystem::path> get_default_cache_root();
char const *get_default_cache_root_env_vars();

void set_env_var(char const *name, char const *value);

[[noreturn]] void terminate_process();

bool is_tty();

}  // namespace envy::platform

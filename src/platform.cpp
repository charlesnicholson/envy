#include "platform.h"

#include <cstdlib>
#include <system_error>

#ifdef _WIN32

#include "platform_windows.h"

namespace envy::platform {

std::optional<std::filesystem::path> get_default_cache_root() {
  if (char const *local_app_data{ std::getenv("LOCALAPPDATA") }) {
    return std::filesystem::path{ local_app_data } / "envy";
  }

  if (char const *user_profile{ std::getenv("USERPROFILE") }) {
    return std::filesystem::path{ user_profile } / "AppData" / "Local" / "envy";
  }

  return std::nullopt;
}

char const *get_default_cache_root_env_vars() { return "LOCALAPPDATA or USERPROFILE"; }

std::intptr_t lock_file(std::filesystem::path const &path) {
  HANDLE const h{ ::CreateFileW(path.c_str(),
                                GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                nullptr,
                                OPEN_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL,
                                nullptr) };

  if (h == INVALID_HANDLE_VALUE) {
    throw std::system_error(::GetLastError(),
                            std::system_category(),
                            "Failed to open lock file: " + path.string());
  }

  OVERLAPPED ovlp{ 0 };
  if (!::LockFileEx(h, LOCKFILE_EXCLUSIVE_LOCK, 0, MAXDWORD, MAXDWORD, &ovlp)) {
    DWORD const err{ ::GetLastError() };
    ::CloseHandle(h);
    throw std::system_error(err,
                            std::system_category(),
                            "Failed to acquire exclusive lock: " + path.string());
  }

  return reinterpret_cast<std::intptr_t>(h);
}

void unlock_file(std::intptr_t handle) {
  if (handle != kInvalidLockHandle) {
    HANDLE h{ reinterpret_cast<HANDLE>(handle) };
    OVERLAPPED ovlp{ 0 };
    ::UnlockFileEx(h, 0, MAXDWORD, MAXDWORD, &ovlp);
    ::CloseHandle(h);
  }
}

void atomic_rename(std::filesystem::path const &from, std::filesystem::path const &to) {
  if (!::MoveFileExW(from.c_str(),
                     to.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    throw std::system_error(::GetLastError(),
                            std::system_category(),
                            "Failed to rename " + from.string() + " to " + to.string());
  }
}

[[noreturn]] void terminate_process() { ::TerminateProcess(::GetCurrentProcess(), 1); }

}  // namespace envy::platform

#else  // POSIX

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>

namespace envy::platform {

std::optional<std::filesystem::path> get_default_cache_root() {
#ifdef __APPLE__
  if (char const *home{ std::getenv("HOME") }) {
    return std::filesystem::path{ home } / "Library" / "Caches" / "envy";
  }
#else
  if (char const *xdg_cache{ std::getenv("XDG_CACHE_HOME") }) {
    return std::filesystem::path{ xdg_cache } / "envy";
  }

  if (char const *home{ std::getenv("HOME") }) {
    return std::filesystem::path{ home } / ".cache" / "envy";
  }
#endif

  return std::nullopt;
}

char const *get_default_cache_root_env_vars() {
#ifdef __APPLE__
  return "HOME";
#else
  return "XDG_CACHE_HOME or HOME";
#endif
}

std::intptr_t lock_file(std::filesystem::path const &path) {
  int const fd{ ::open(path.c_str(), O_CREAT | O_RDWR, 0666) };
  if (fd == -1) {
    throw std::system_error(errno,
                            std::system_category(),
                            "Failed to open lock file: " + path.string());
  }

  struct flock fl{ .l_start = 0,
                   .l_len = 0,
                   .l_pid = 0,
                   .l_type = F_WRLCK,
                   .l_whence = SEEK_SET };

  if (::fcntl(fd, F_SETLKW, &fl) == -1) {
    int const err{ errno };
    ::close(fd);
    throw std::system_error(err,
                            std::system_category(),
                            "Failed to acquire exclusive lock: " + path.string());
  }

  return fd;
}

void unlock_file(std::intptr_t handle) {
  if (handle != kInvalidLockHandle) { ::close(static_cast<int>(handle)); }
}

void atomic_rename(std::filesystem::path const &from, std::filesystem::path const &to) {
  if (::rename(from.c_str(), to.c_str()) != 0) {
    throw std::system_error(errno,
                            std::system_category(),
                            "Failed to rename " + from.string() + " to " + to.string());
  }
}

[[noreturn]] void terminate_process() { std::abort(); }

}  // namespace envy::platform

#endif

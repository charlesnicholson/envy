#include "platform.h"

#include <cstdlib>
#include <stdexcept>
#include <string>
#include <system_error>

#ifdef _WIN32

#include "platform_windows.h"

namespace envy::platform {

struct file_lock::impl {
  HANDLE handle;
};

file_lock::~file_lock() {
  if (impl_) {
    OVERLAPPED ovlp{};
    ::UnlockFileEx(impl_->handle, 0, MAXDWORD, MAXDWORD, &ovlp);
    ::CloseHandle(impl_->handle);
  }
}

file_lock::file_lock(file_lock &&) noexcept = default;
file_lock &file_lock::operator=(file_lock &&) noexcept = default;

file_lock::operator bool() const { return impl_ != nullptr; }

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

void set_env_var(char const *name, char const *value) {
  if (name == nullptr || value == nullptr) {
    throw std::invalid_argument("set_env_var: null name or value");
  }

  if (::_putenv_s(name, value) != 0) {
    throw std::runtime_error(std::string("set_env_var: failed to set ") + name);
  }
}

file_lock::file_lock(std::filesystem::path const &path) {
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

  OVERLAPPED ovlp{};
  if (!::LockFileEx(h, LOCKFILE_EXCLUSIVE_LOCK, 0, MAXDWORD, MAXDWORD, &ovlp)) {
    DWORD const err{ ::GetLastError() };
    ::CloseHandle(h);
    throw std::system_error(err,
                            std::system_category(),
                            "Failed to acquire file lock: " + path.string());
  }

  impl_ = std::make_unique<impl>();
  impl_->handle = h;
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

void touch_file(std::filesystem::path const &path) {
  HANDLE const h{ ::CreateFileW(path.c_str(),
                                GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                nullptr,
                                CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL,
                                nullptr) };

  if (h == INVALID_HANDLE_VALUE) {
    throw std::system_error(::GetLastError(),
                            std::system_category(),
                            "Failed to touch file: " + path.string());
  }

  ::CloseHandle(h);
}

[[noreturn]] void terminate_process() { ::TerminateProcess(::GetCurrentProcess(), 1); }

bool is_tty() { return ::_isatty(::_fileno(stderr)) != 0; }

}  // namespace envy::platform

#else  // POSIX

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <mutex>
#include <unordered_map>

namespace envy::platform {

struct file_lock::impl {
  int fd;
  std::mutex *path_mutex;  // Pointer to mutex in lock_mutexes

  // POSIX file locks are per-process, not per-thread. If thread A acquires a lock
  // and thread B in the same process tries to lock, B succeeds immediately (both
  // think they got it). Windows LockFileEx blocks thread B automatically. To handle
  // this on POSIX, we use an in-process mutex per lock path.
  static std::mutex s_lock_map_mutex;
  static std::unordered_map<std::string, std::unique_ptr<std::mutex> > s_lock_mutexes;
};

std::mutex file_lock::impl::s_lock_map_mutex;
std::unordered_map<std::string, std::unique_ptr<std::mutex> >
    file_lock::impl::s_lock_mutexes;

file_lock::~file_lock() {
  if (impl_) {
    ::close(impl_->fd);
    if (impl_->path_mutex) { impl_->path_mutex->unlock(); }
  }
}

file_lock::file_lock(file_lock &&) noexcept = default;
file_lock &file_lock::operator=(file_lock &&) noexcept = default;

file_lock::operator bool() const { return impl_ != nullptr; }

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

void set_env_var(char const *name, char const *value) {
  if (name == nullptr || value == nullptr) {
    throw std::invalid_argument("set_env_var: null name or value");
  }

  if (::setenv(name, value, 1) != 0) {
    throw std::runtime_error(std::string("set_env_var: failed to set ") + name);
  }
}

file_lock::file_lock(std::filesystem::path const &path) {
  // Canonicalize path to ensure different representations of same path use same mutex
  std::string const canonical_key{
    std::filesystem::absolute(path).lexically_normal().string()
  };

  // Acquire in-process mutex for this lock path, ensure one thread per cache entry
  std::unique_lock<std::mutex> path_lock{ [&]() {
    std::lock_guard<std::mutex> lock(impl::s_lock_map_mutex);
    auto &mutex_ptr{ impl::s_lock_mutexes[canonical_key] };
    if (!mutex_ptr) { mutex_ptr = std::make_unique<std::mutex>(); }
    return std::unique_lock<std::mutex>{ *mutex_ptr };
  }() };

  // Now acquire the file lock (blocks other processes)
  int const fd{ ::open(path.c_str(), O_CREAT | O_RDWR, 0666) };
  if (fd == -1) {
    throw std::system_error(errno,
                            std::system_category(),
                            "Failed to open lock file: " + path.string());
  }

  struct flock fl{ .l_type = F_WRLCK,
                   .l_whence = SEEK_SET,
                   .l_start = 0,
                   .l_len = 0,
                   .l_pid = 0 };

  if (::fcntl(fd, F_SETLKW, &fl) == -1) {
    int const err{ errno };
    ::close(fd);
    throw std::system_error(err,
                            std::system_category(),
                            "Failed to acquire exclusive lock: " + path.string());
  }

  impl_ = std::make_unique<impl>();
  impl_->fd = fd;
  impl_->path_mutex = path_lock.release();  // Transfer ownership, mutex stays locked
}

void atomic_rename(std::filesystem::path const &from, std::filesystem::path const &to) {
  if (::rename(from.c_str(), to.c_str()) != 0) {
    throw std::system_error(errno,
                            std::system_category(),
                            "Failed to rename " + from.string() + " to " + to.string());
  }
}

void touch_file(std::filesystem::path const &path) {
  int const fd{ ::open(path.c_str(), O_CREAT | O_WRONLY, 0644) };
  if (fd == -1) {
    throw std::system_error(errno,
                            std::system_category(),
                            "Failed to touch file: " + path.string());
  }
  ::close(fd);
}

[[noreturn]] void terminate_process() { std::abort(); }

bool is_tty() { return ::isatty(::fileno(stderr)) != 0; }

}  // namespace envy::platform

#endif

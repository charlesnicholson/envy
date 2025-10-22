#include "file_lock.h"

#include <system_error>

#ifdef _WIN32

#include "platform_windows.h"

namespace envy {

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

  OVERLAPPED ovlp{ 0 };
  if (!::LockFileEx(h, LOCKFILE_EXCLUSIVE_LOCK, 0, MAXDWORD, MAXDWORD, &ovlp)) {
    DWORD const err{ ::GetLastError() };
    ::CloseHandle(h);
    throw std::system_error(err,
                            std::system_category(),
                            "Failed to acquire exclusive lock: " + path.string());
  }

  handle_ = reinterpret_cast<std::intptr_t>(h);
}

file_lock::~file_lock() {
  if (handle_ != 0) {
    HANDLE h{ reinterpret_cast<HANDLE>(handle_) };
    OVERLAPPED ovlp{ 0 };
    ::UnlockFileEx(h, 0, MAXDWORD, MAXDWORD, &ovlp);
    ::CloseHandle(h);
  }
}

}  // namespace envy

#else  // POSIX (Linux, macOS, etc.)

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>

namespace envy {

file_lock::file_lock(std::filesystem::path const &path) {
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

  handle_ = fd;
}

file_lock::~file_lock() {
  if (handle_ != -1) { ::close(static_cast<int>(handle_)); }
}

}  // namespace envy

#endif

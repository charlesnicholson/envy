#include "platform.h"

#include <fcntl.h>
#include <unistd.h>
#include <wordexp.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

extern "C" char **environ;

namespace envy::platform {

struct file_lock::impl {
  int fd;
  std::mutex *path_mutex;  // owned by the s_lock_mutexes map
  std::filesystem::path lock_path;

  // POSIX file locks are per-process, not per-thread: multiple threads in the same process
  // can bypass the file lock and acquire it simultaneously. To ensure thread-level mutual
  // exclusion for file locks within a process, we use an in-process mutex per path.
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

    std::error_code ec;  // Ignore errors - lock file may  be deleted or inaccessible.
    std::filesystem::remove(impl_->lock_path, ec);
  }
}

file_lock::file_lock(file_lock &&) noexcept = default;
file_lock &file_lock::operator=(file_lock &&) noexcept = default;

file_lock::operator bool() const { return impl_ != nullptr; }

std::optional<std::filesystem::path> get_default_cache_root() {
  // ENVY_CACHE_ROOT takes precedence
  if (char const *env_root{ std::getenv("ENVY_CACHE_ROOT") }) {
    return std::filesystem::path{ env_root };
  }

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

std::filesystem::path get_exe_path() {
#ifdef __APPLE__
  uint32_t size{ 0 };
  _NSGetExecutablePath(nullptr, &size);
  std::vector<char> buf(size);
  if (_NSGetExecutablePath(buf.data(), &size) != 0) {
    throw std::runtime_error("_NSGetExecutablePath failed");
  }
  return std::filesystem::canonical(buf.data());
#else
  std::vector<char> buf(4096);
  ssize_t const len{ ::readlink("/proc/self/exe", buf.data(), buf.size() - 1) };
  if (len == -1) {
    throw std::system_error(errno,
                            std::system_category(),
                            "readlink /proc/self/exe failed");
  }
  buf[static_cast<size_t>(len)] = '\0';
  return std::filesystem::path{ buf.data() };
#endif
}

void env_var_set(char const *name, char const *value) {
  if (name == nullptr || value == nullptr) {
    throw std::invalid_argument("env_var_set: null name or value");
  }

  if (::setenv(name, value, 1) != 0) {
    throw std::runtime_error(std::string("env_var_set: failed to set ") + name);
  }
}

void env_var_unset(char const *name) {
  if (name == nullptr) { throw std::invalid_argument("env_var_unset: null name"); }
  ::unsetenv(name);
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
  impl_->lock_path = path;
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

void flush_directory(std::filesystem::path const &) {
  // No-op on Unix - directory metadata is not cached in the same way as Windows
}

bool file_exists(std::filesystem::path const &path) {
  // On Unix, directory caching isn't an issue - std::filesystem::exists is sufficient
  return std::filesystem::exists(path);
}

[[noreturn]] void terminate_process() { std::abort(); }

bool is_tty() { return ::isatty(::fileno(stderr)) != 0; }

platform_id native() { return platform_id::POSIX; }

std::string_view os_name() {
#if defined(__APPLE__) && defined(__MACH__)
  return "darwin";
#elif defined(__linux__)
  return "linux";
#else
#error "unsupported POSIX OS"
#endif
}

std::string_view arch_name() {
#if defined(__aarch64__) || defined(__arm64__)
  return
#if defined(__APPLE__)
      "arm64";
#else
      "aarch64";
#endif
#elif defined(__x86_64__)
  return "x86_64";
#else
#error "unsupported architecture"
#endif
}

std::error_code remove_all_with_retry(std::filesystem::path const &target) {
  // On POSIX, file deletion works even with open handles (files get unlinked
  // but data persists until all handles close). No retry needed.
  std::error_code ec;
  std::filesystem::remove_all(target, ec);
  return ec;
}

std::filesystem::path expand_path(std::string_view p) {
  if (p.empty()) { return {}; }

  wordexp_t we{};
  std::string const path_str{ p };
  int const flags{ WRDE_NOCMD | WRDE_UNDEF };  // no $(cmd), fail on undefined $VAR

  int const rc{ wordexp(path_str.c_str(), &we, flags) };

  if (rc == 0) {
    if (we.we_wordc == 0) {
      wordfree(&we);
      throw std::runtime_error("path expansion produced no results: " + path_str);
    }
    std::filesystem::path result{ we.we_wordv[0] };
    wordfree(&we);
    return result;
  }

  // POSIX: wordfree() must only be called after successful wordexp()
  if (rc == WRDE_BADVAL) {
    throw std::runtime_error("undefined variable in path: " + path_str);
  }
  throw std::runtime_error("path expansion failed: " + path_str);
}

std::vector<std::string> get_environment() {
  std::vector<std::string> result;
  for (char **ep = environ; *ep; ++ep) { result.emplace_back(*ep); }
  return result;
}

int exec_process(std::filesystem::path const &binary,
                 char **argv,
                 std::vector<std::string> env) {
  std::vector<char *> envp;
  envp.reserve(env.size() + 1);
  for (auto &e : env) { envp.push_back(e.data()); }
  envp.push_back(nullptr);

  execve(binary.c_str(), argv, envp.data());
  throw std::runtime_error(std::string{ "exec_process: execve failed: " } +
                           std::strerror(errno));
}

}  // namespace envy::platform

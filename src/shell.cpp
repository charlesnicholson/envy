#include "shell.h"

#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#if defined(_WIN32)

namespace envy {

shell_env_t shell_getenv() {
  throw std::runtime_error("shell_getenv is not implemented on Windows yet");
}

int shell_run(std::string_view /*script*/, shell_invocation const & /*invocation*/) {
  throw std::runtime_error("shell_run is not implemented on Windows yet");
}

}  // namespace envy

#else  // POSIX implementation

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>

#include <filesystem>

extern char **environ;

namespace envy {
namespace {

class file_cleanup {
 public:
  explicit file_cleanup(std::filesystem::path path) : path_{ std::move(path) } {}
  ~file_cleanup() {
    if (path_.empty()) { return; }
    std::error_code ec;
    std::filesystem::remove(path_, ec);
  }

  file_cleanup(file_cleanup const &) = delete;
  file_cleanup &operator=(file_cleanup const &) = delete;

 private:
  std::filesystem::path path_;
};

class fd_cleanup {
 public:
  explicit fd_cleanup(int fd) : fd_{ fd } {}
  ~fd_cleanup() {
    if (fd_ == -1) { return; }
    while (::close(fd_) == -1 && errno == EINTR) {}
  }

  fd_cleanup(fd_cleanup const &) = delete;
  fd_cleanup &operator=(fd_cleanup const &) = delete;

  int get() const { return fd_; }
  int release() {
    int const fd = fd_;
    fd_ = -1;
    return fd;
  }

 private:
  int fd_{ -1 };
};

void close_no_throw(int fd) {
  if (fd == -1) { return; }
  while (::close(fd) == -1 && errno == EINTR) {}
}

std::string create_temp_script(std::string_view script) {
  auto tmp_dir{ std::filesystem::temp_directory_path() };
  std::string pattern{ (tmp_dir / "envy-shell-XXXXXX").string() };

  std::vector<char> path_buffer{ pattern.begin(), pattern.end() };
  path_buffer.push_back('\0');

  int const fd{ ::mkstemp(path_buffer.data()) };
  if (fd == -1) {
    throw std::system_error(errno, std::generic_category(), "mkstemp failed");
  }
  fd_cleanup fd_guard{ fd };

  std::string content{ "#!/bin/bash\nset -euo pipefail\n" };
  content.append(script);
  if (content.empty() || content.back() != '\n') { content.push_back('\n'); }

  ssize_t remaining{ static_cast<ssize_t>(content.size()) };
  char const *data{ content.data() };
  while (remaining > 0) {
    ssize_t const written{ ::write(fd_guard.get(), data, remaining) };
    if (written == -1) {
      if (errno == EINTR) { continue; }
      throw std::system_error(errno, std::generic_category(), "write failed");
    }
    remaining -= written;
    data += written;
  }

  if (::fchmod(fd_guard.get(), S_IRUSR | S_IWUSR | S_IXUSR) == -1) {
    throw std::system_error(errno, std::generic_category(), "fchmod failed");
  }

  // Ensure content is flushed to disk before executing.
  if (::fsync(fd_guard.get()) == -1) {
    throw std::system_error(errno, std::generic_category(), "fsync failed");
  }

  int const raw_fd{ fd_guard.release() };
  if (::close(raw_fd) == -1) {
    throw std::system_error(errno, std::generic_category(), "close failed");
  }

  return std::string{ path_buffer.data() };
}

void stream_pipe_lines(int fd, shell_output_cb_t const &callback) {
  std::string pending;
  pending.reserve(256);
  std::string chunk(4096, '\0');

  while (true) {
    ssize_t const read_bytes{ ::read(fd, chunk.data(), chunk.size()) };
    if (read_bytes == 0) { break; }
    if (read_bytes == -1) {
      if (errno == EINTR) { continue; }
      throw std::system_error(errno, std::generic_category(), "read failed");
    }

    pending.append(chunk.data(), static_cast<size_t>(read_bytes));
    size_t newline{ 0 };
    while ((newline = pending.find('\n')) != std::string::npos) {
      std::string line{ pending.substr(0, newline) };
      callback(line);
      pending.erase(0, newline + 1);
    }
  }

  if (!pending.empty()) { callback(pending); }
}

std::vector<char *> build_envp(std::vector<std::string> &storage) {
  std::vector<char *> result;
  result.reserve(storage.size() + 1);
  for (auto &entry : storage) { result.push_back(entry.data()); }
  result.push_back(nullptr);
  return result;
}

std::vector<std::string> serialize_env(shell_env_t const &env) {
  std::vector<std::string> storage;
  storage.reserve(env.size());
  for (auto const &[key, value] : env) { storage.push_back(key + "=" + value); }
  return storage;
}

int wait_for_child(pid_t child) {
  int status{ 0 };
  while (true) {
    pid_t const result = ::waitpid(child, &status, 0);
    if (result == -1 && errno == EINTR) { continue; }
    if (result == -1) {
      throw std::system_error(errno, std::generic_category(), "waitpid failed");
    }
    break;
  }

  if (WIFEXITED(status)) { return WEXITSTATUS(status); }
  if (WIFSIGNALED(status)) { return 128 + WTERMSIG(status); }
  return status;
}

}  // namespace

shell_env_t shell_getenv() {
  shell_env_t env;
  if (!environ) { return env; }

  for (char **entry{ environ }; *entry != nullptr; ++entry) {
    std::string_view kv{ *entry };
    size_t const sep{ kv.find('=') };
    if (sep == std::string_view::npos) { continue; }
    std::string key{ kv.substr(0, sep) };
    std::string value{ kv.substr(sep + 1) };
    env[std::move(key)] = std::move(value);
  }

  return env;
}

int shell_run(std::string_view script, shell_invocation const &invocation) {
  if (!invocation.on_output_line) {
    throw std::invalid_argument("shell_run requires on_output_line callback");
  }

  std::string script_path{ create_temp_script(script) };
  file_cleanup cleanup{ script_path };

  std::vector<std::string> env_storage{ serialize_env(invocation.env) };
  auto envp{ build_envp(env_storage) };

  int pipefd[2];
  if (::pipe(pipefd) == -1) {
    throw std::system_error(errno, std::generic_category(), "pipe failed");
  }
  fd_cleanup read_end{ pipefd[0] };
  fd_cleanup write_end{ pipefd[1] };

  std::string shell_arg{ "/bin/bash" };
  std::string script_arg{ script_path };

  pid_t const child{ ::fork() };
  if (child == -1) {
    throw std::system_error(errno, std::generic_category(), "fork failed");
  }

  if (child == 0) {  // Child process
    ::close(read_end.release());
    if (::dup2(write_end.get(), STDOUT_FILENO) == -1 ||
        ::dup2(write_end.get(), STDERR_FILENO) == -1) {
      std::perror("dup2");
      _exit(127);
    }

    ::close(write_end.release());  // Close original write descriptor after dup.

    if (invocation.cwd) {
      if (::chdir(invocation.cwd->c_str()) == -1) {
        std::perror("chdir");
        _exit(127);
      }
    }

    std::vector<char *> argv{ shell_arg.data(), script_arg.data(), nullptr };

    ::execve(shell_arg.c_str(), argv.data(), envp.data());
    std::perror("execve");
    _exit(127);
  }

  close_no_throw(write_end.release());  // Parent

  try {
    stream_pipe_lines(read_end.get(), invocation.on_output_line);
  } catch (...) {
    ::kill(child, SIGKILL);
    close_no_throw(read_end.release());
    wait_for_child(child);
    throw;
  }

  close_no_throw(read_end.release());
  return wait_for_child(child);
}

}  // namespace envy

#endif  // POSIX implementation

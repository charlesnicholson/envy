#if defined(_WIN32)
#error "shell_posix.cpp should not be compiled on Windows builds"
#else

#include "shell.h"

#include "util.h"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

namespace envy {
namespace {

constexpr int kChildErrorExit{ 127 };
constexpr size_t kPipeBufferSize{ 4096 };
constexpr size_t kLinePendingReserve{ 256 };
constexpr int kSignalExitBase{ 128 };

class fd_cleanup {
 public:
  explicit fd_cleanup(int fd) : fd_{ fd } {}
  ~fd_cleanup() {
    if (fd_ == -1) { return; }
    close_with_retry();
  }

  fd_cleanup(fd_cleanup const &) = delete;
  fd_cleanup &operator=(fd_cleanup const &) = delete;

  int get() const { return fd_; }
  void release() {
    if (fd_ == -1) { return; }
    close_with_retry();
    fd_ = -1;
  }

 private:
  void close_with_retry() {
    for (int attempts{ 0 }; attempts < 3 && ::close(fd_) == -1; ++attempts) {
      if (errno != EINTR) { break; }
    }
  }

  int fd_{ -1 };
};

std::string get_shell_path(shell_choice choice) {
  switch (choice) {
    case shell_choice::bash:
      if (char const *bash_env{ ::getenv("BASH") }) { return bash_env; }
      return "/usr/bin/env bash";
    case shell_choice::sh: return "/bin/sh";
    default: throw std::invalid_argument("shell_run: unsupported shell choice on POSIX");
  }
}

std::string create_temp_script(std::string_view script, shell_choice choice) {
  auto tmp_dir{ std::filesystem::temp_directory_path() };
  std::string pattern{ (tmp_dir / "envy-shell-XXXXXX").string() };

  std::vector<char> path_buffer{ pattern.begin(), pattern.end() };
  path_buffer.push_back('\0');

  int const fd{ ::mkstemp(path_buffer.data()) };
  if (fd == -1) {
    throw std::system_error(errno, std::generic_category(), "mkstemp failed");
  }
  fd_cleanup fd_guard{ fd };

  std::string content{ "#!" + get_shell_path(choice) + "\n" };
  content.append(script);
  if (content.back() != '\n') { content.push_back('\n'); }

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

  if (::fsync(fd_guard.get()) == -1) {
    throw std::system_error(errno, std::generic_category(), "fsync failed");
  }

  return std::string{ path_buffer.data() };
}

void stream_pipe_lines(int fd, std::function<void(std::string_view)> const &callback) {
  std::string pending;
  pending.reserve(kLinePendingReserve);
  std::string chunk(kPipeBufferSize, '\0');

  ssize_t read_bytes;
  while ((read_bytes = ::read(fd, chunk.data(), chunk.size())) != 0) {
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

struct envp_storage {
  std::vector<std::string> strings;
  std::vector<char *> pointers;
};

envp_storage build_envp(shell_env_t const &env) {
  envp_storage result;
  result.strings.reserve(env.size());
  result.pointers.reserve(env.size() + 1);

  for (auto const &[key, value] : env) { result.strings.push_back(key + "=" + value); }
  for (auto &entry : result.strings) { result.pointers.push_back(entry.data()); }
  result.pointers.push_back(nullptr);

  return result;
}

shell_result wait_for_child(pid_t child) {
  int status{ 0 };
  while (true) {
    pid_t const result = ::waitpid(child, &status, 0);
    if (result == -1 && errno == EINTR) { continue; }
    if (result == -1) {
      throw std::system_error(errno, std::generic_category(), "waitpid failed");
    }
    break;
  }

  if (WIFEXITED(status)) {
    return { .exit_code = WEXITSTATUS(status), .signal = std::nullopt };
  }

  if (WIFSIGNALED(status)) {
    int const sig{ WTERMSIG(status) };
    return { .exit_code = kSignalExitBase + sig, .signal = sig };
  }

  return { .exit_code = status, .signal = std::nullopt };
}

[[noreturn]] void exec_child_process(fd_cleanup &read_end,
                                     fd_cleanup &write_end,
                                     std::optional<std::filesystem::path> const &cwd,
                                     std::string const &shell_arg,
                                     std::string const &script_arg,
                                     envp_storage const &envp) {
  read_end.release();

  int const null_fd{ ::open("/dev/null", O_RDONLY) };
  if (null_fd == -1 || ::dup2(null_fd, STDIN_FILENO) == -1 ||
      ::dup2(write_end.get(), STDOUT_FILENO) == -1 ||
      ::dup2(write_end.get(), STDERR_FILENO) == -1) {
    std::perror("dup2");
    _exit(kChildErrorExit);
  }
  if (null_fd != STDIN_FILENO) { ::close(null_fd); }
  write_end.release();

  if (cwd) {
    if (::chdir(cwd->c_str()) == -1) {
      std::perror("chdir");
      _exit(kChildErrorExit);
    }
  }

  // Split shell_arg by spaces to handle "/usr/bin/env bash"
  std::vector<std::string> shell_parts;
  std::string_view shell_view{ shell_arg };
  size_t start{ 0 };
  while (start < shell_view.size()) {
    while (start < shell_view.size() && shell_view[start] == ' ') { ++start; }
    if (start >= shell_view.size()) { break; }
    size_t const end{ shell_view.find(' ', start) };
    if (end == std::string_view::npos) {
      shell_parts.emplace_back(shell_view.substr(start));
      break;
    }
    shell_parts.emplace_back(shell_view.substr(start, end - start));
    start = end + 1;
  }

  if (shell_parts.empty()) {
    std::fprintf(stderr, "invalid shell path: '%s'\n", shell_arg.c_str());
    _exit(kChildErrorExit);
  }

  std::vector<char *> argv;
  argv.reserve(shell_parts.size() + 2);
  for (auto &part : shell_parts) { argv.push_back(const_cast<char *>(part.c_str())); }
  argv.push_back(const_cast<char *>(script_arg.c_str()));
  argv.push_back(nullptr);

  ::execve(shell_parts[0].c_str(), argv.data(), envp.pointers.data());
  std::perror("execve");
  _exit(kChildErrorExit);
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

shell_result shell_run(std::string_view script, shell_run_cfg const &cfg) {
  if (cfg.shell != shell_choice::bash && cfg.shell != shell_choice::sh) {
    throw std::invalid_argument("shell_run: POSIX build only supports bash or sh shells");
  }

  std::string script_path{ create_temp_script(script, cfg.shell) };
  scoped_path_cleanup cleanup{ std::filesystem::path{ script_path } };

  auto envp{ build_envp(cfg.env) };

  int pipefd[2];
  if (::pipe(pipefd) == -1) {
    throw std::system_error(errno, std::generic_category(), "pipe failed");
  }

  fd_cleanup read_end{ pipefd[0] };
  fd_cleanup write_end{ pipefd[1] };

  std::string shell_arg{ get_shell_path(cfg.shell) };
  std::string script_arg{ script_path };

  pid_t const child{ ::fork() };
  if (child == -1) {
    throw std::system_error(errno, std::generic_category(), "fork failed");
  }

  if (child == 0) {
    exec_child_process(read_end, write_end, cfg.cwd, shell_arg, script_arg, envp);
  }

  write_end.release();  // Parent: close write end and stream output

  shell_result result;
  try {
    stream_pipe_lines(read_end.get(), cfg.on_output_line);
    result = wait_for_child(child);
  } catch (...) {
    ::kill(child, SIGKILL);
    wait_for_child(child);
    throw;
  }

  return result;
}

}  // namespace envy

#endif  // POSIX implementation

#if defined(_WIN32)
#error "shell_posix.cpp should not be compiled on Windows builds"
#else

#include "shell.h"

#include "util.h"

#include <array>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <poll.h>
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
  fd_cleanup(fd_cleanup &&other) noexcept : fd_{ other.fd_ } { other.fd_ = -1; }

  fd_cleanup &operator=(fd_cleanup &&other) noexcept {
    if (this == &other) { return *this; }
    if (fd_ != -1) { close_with_retry(); }
    fd_ = other.fd_;
    other.fd_ = -1;
    return *this;
  }

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

  // Write script content as-is (no shebang needed - we invoke shell explicitly)
  std::string content{ script };
  if (!content.empty() && content.back() != '\n') { content.push_back('\n'); }

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

void dispatch_line(shell_run_cfg const &cfg, shell_stream stream, std::string_view line) {
  if (stream == shell_stream::std_out) {
    if (cfg.on_stdout_line) { cfg.on_stdout_line(line); }
  } else {
    if (cfg.on_stderr_line) { cfg.on_stderr_line(line); }
  }

  if (cfg.on_output_line) { cfg.on_output_line(line); }
}

struct pipe_state {
  fd_cleanup read_fd;
  shell_stream stream;
  std::string pending;
  bool closed{ false };
};

void stream_pipes(std::array<pipe_state, 2> &pipes, shell_run_cfg const &cfg) {
  std::array<pollfd, 2> poll_fds{};
  std::string chunk(kPipeBufferSize, '\0');
  size_t closed_count{ 0 };

  for (auto &pipe : pipes) { pipe.pending.reserve(kLinePendingReserve); }

  for (size_t i{ 0 }; i < pipes.size(); ++i) {
    poll_fds[i].fd = pipes[i].closed ? -1 : pipes[i].read_fd.get();
    poll_fds[i].events = pipes[i].closed ? 0 : POLLIN;
    poll_fds[i].revents = 0;
  }

  while (closed_count < pipes.size()) {
    int const poll_result{ ::poll(poll_fds.data(), poll_fds.size(), -1) };
    if (poll_result == -1) {
      if (errno == EINTR) { continue; }
      throw std::system_error(errno, std::generic_category(), "poll failed");
    }

    for (size_t i{ 0 }; i < pipes.size(); ++i) {
      if (pipes[i].closed) { continue; }

      short const revents{ poll_fds[i].revents };
      if (revents == 0) { continue; }
      if (revents & (POLLERR | POLLNVAL)) {
        throw std::runtime_error("poll failed on child pipe");
      }

      ssize_t const read_bytes{
        ::read(pipes[i].read_fd.get(), chunk.data(), chunk.size())
      };

      if (read_bytes == -1) {
        if (errno == EINTR) { continue; }
        throw std::system_error(errno, std::generic_category(), "read failed");
      }

      if (read_bytes == 0) {
        if (!pipes[i].pending.empty()) {
          dispatch_line(cfg, pipes[i].stream, pipes[i].pending);
          pipes[i].pending.clear();
        }

        pipes[i].closed = true;
        ++closed_count;

        poll_fds[i].fd = -1;
        poll_fds[i].events = 0;
        poll_fds[i].revents = 0;
        continue;
      }

      pipes[i].pending.append(chunk.data(), static_cast<size_t>(read_bytes));

      size_t newline{ 0 };
      while ((newline = pipes[i].pending.find('\n')) != std::string::npos) {
        std::string line{ pipes[i].pending.substr(0, newline) };
        dispatch_line(cfg, pipes[i].stream, line);
        pipes[i].pending.erase(0, newline + 1);
      }
    }
  }
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

[[noreturn]] void exec_child_process(fd_cleanup &stdout_read,
                                     fd_cleanup &stdout_write,
                                     fd_cleanup &stderr_read,
                                     fd_cleanup &stderr_write,
                                     std::optional<std::filesystem::path> const &cwd,
                                     std::vector<std::string> const &argv_strings,
                                     envp_storage const &envp) {
  stdout_read.release();
  stderr_read.release();

  int const null_fd{ ::open("/dev/null", O_RDONLY) };
  if (null_fd == -1 || ::dup2(null_fd, STDIN_FILENO) == -1 ||
      ::dup2(stdout_write.get(), STDOUT_FILENO) == -1 ||
      ::dup2(stderr_write.get(), STDERR_FILENO) == -1) {
    std::perror("dup2");
    _exit(kChildErrorExit);
  }
  if (null_fd != STDIN_FILENO) { ::close(null_fd); }
  stdout_write.release();
  stderr_write.release();

  if (cwd) {
    if (::chdir(cwd->c_str()) == -1) {
      std::perror("chdir");
      _exit(kChildErrorExit);
    }
  }

  if (argv_strings.empty()) {
    std::fprintf(stderr, "exec_child_process: argv must be non-empty\n");
    _exit(kChildErrorExit);
  }

  // Build argv array from strings
  std::vector<char *> argv;
  argv.reserve(argv_strings.size() + 1);
  for (auto const &arg : argv_strings) { argv.push_back(const_cast<char *>(arg.c_str())); }
  argv.push_back(nullptr);

  ::execve(argv_strings[0].c_str(), argv.data(), envp.pointers.data());
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
  // Determine if we need a script file or pass inline
  bool const is_inline{ std::holds_alternative<custom_shell_inline>(cfg.shell) };

  std::string script_path{};
  std::unique_ptr<scoped_path_cleanup> cleanup;

  if (!is_inline) {
    script_path = create_temp_script(script);
    cleanup = std::make_unique<scoped_path_cleanup>(std::filesystem::path{ script_path });
  }

  // Build argv based on shell type
  std::vector<std::string> argv_strings{ std::visit(
      match{
          [&script_path](shell_choice const &shell_cfg) -> std::vector<std::string> {
            if (shell_cfg != shell_choice::bash && shell_cfg != shell_choice::sh) {
              throw std::invalid_argument(
                  "shell_run: POSIX only supports bash or sh built-in shells");
            }
            // Split shell path to handle "/usr/bin/env bash"
            std::vector<std::string> result;
            std::string const shell_path{ get_shell_path(shell_cfg) };
            std::string_view shell_view{ shell_path };
            size_t start{ 0 };

            while (start < shell_view.size()) {
              while (start < shell_view.size() && shell_view[start] == ' ') { ++start; }

              if (start >= shell_view.size()) { break; }
              size_t const end{ shell_view.find(' ', start) };

              if (end == std::string_view::npos) {
                result.emplace_back(shell_view.substr(start));
                break;
              }

              result.emplace_back(shell_view.substr(start, end - start));
              start = end + 1;
            }

            result.push_back(script_path);
            return result;
          },
          [&script_path](custom_shell_file const &shell_cfg) -> std::vector<std::string> {
            // Copy argv and append script path
            std::vector<std::string> result{ shell_cfg.argv };
            result.push_back(script_path);
            return result;
          },
          [&script](custom_shell_inline const &shell_cfg) -> std::vector<std::string> {
            // custom_shell_inline: copy argv and append script content
            std::vector<std::string> result{ shell_cfg.argv };
            result.push_back(std::string{ script });
            return result;
          },
      },
      cfg.shell) };

  auto envp{ build_envp(cfg.env) };

  int stdout_pipefd[2];
  if (::pipe(stdout_pipefd) == -1) {
    throw std::system_error(errno, std::generic_category(), "pipe failed");
  }

  int stderr_pipefd[2];
  if (::pipe(stderr_pipefd) == -1) {
    throw std::system_error(errno, std::generic_category(), "pipe failed");
  }

  fd_cleanup stdout_read_end{ stdout_pipefd[0] };
  fd_cleanup stdout_write_end{ stdout_pipefd[1] };
  fd_cleanup stderr_read_end{ stderr_pipefd[0] };
  fd_cleanup stderr_write_end{ stderr_pipefd[1] };

  pid_t const child{ ::fork() };
  if (child == -1) {
    throw std::system_error(errno, std::generic_category(), "fork failed");
  }

  if (child == 0) {  // child process exits in exec_child_process
    exec_child_process(stdout_read_end,
                       stdout_write_end,
                       stderr_read_end,
                       stderr_write_end,
                       cfg.cwd,
                       argv_strings,
                       envp);
  }

  stdout_write_end.release();  // Parent: close write ends and stream output
  stderr_write_end.release();

  shell_result result;
  try {
    std::array<pipe_state, 2> pipes{
      pipe_state{ std::move(stdout_read_end), shell_stream::std_out, {}, false },
      pipe_state{ std::move(stderr_read_end), shell_stream::std_err, {}, false },
    };

    stream_pipes(pipes, cfg);
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

#include "platform.h"

#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace envy::platform {

struct file_lock::impl {
  HANDLE handle;
  std::filesystem::path lock_path;
};

file_lock::~file_lock() {
  if (impl_) {
    OVERLAPPED ovlp{};
    ::UnlockFileEx(impl_->handle, 0, MAXDWORD, MAXDWORD, &ovlp);
    ::CloseHandle(impl_->handle);

    // Delete lock file after closing handle
    std::error_code ec;
    std::filesystem::remove(impl_->lock_path, ec);
    // Ignore errors - file may be held by another process, which is expected
  }
}

file_lock::file_lock(file_lock &&) noexcept = default;
file_lock &file_lock::operator=(file_lock &&) noexcept = default;

file_lock::operator bool() const { return impl_ != nullptr; }

std::optional<std::filesystem::path> get_default_cache_root() {
  if (char const *env_root{ std::getenv("ENVY_CACHE_ROOT") }) {
    return std::filesystem::path{ env_root };
  }

  if (char const *local_app_data{ std::getenv("LOCALAPPDATA") }) {
    return std::filesystem::path{ local_app_data } / "envy";
  }

  if (char const *user_profile{ std::getenv("USERPROFILE") }) {
    return std::filesystem::path{ user_profile } / "AppData" / "Local" / "envy";
  }

  return std::nullopt;
}

char const *get_default_cache_root_env_vars() { return "LOCALAPPDATA or USERPROFILE"; }

std::filesystem::path get_exe_path() {
  std::vector<wchar_t> buf(32768);
  if (::GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size())) == 0) {
    throw std::system_error(::GetLastError(),
                            std::system_category(),
                            "GetModuleFileNameW failed");
  }
  return std::filesystem::path{ buf.data() };
}

void env_var_set(char const *name, char const *value) {
  if (name == nullptr || value == nullptr) {
    throw std::invalid_argument("env_var_set: null name or value");
  }

  if (::_putenv_s(name, value) != 0) {
    throw std::runtime_error(std::string("env_var_set: failed to set ") + name);
  }
}

void env_var_unset(char const *name) {
  if (name == nullptr) { throw std::invalid_argument("env_var_unset: null name"); }
  if (::_putenv_s(name, "") != 0) {
    throw std::runtime_error(std::string("env_var_unset: failed to unset ") + name);
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
  impl_->lock_path = path;
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
                                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
                                nullptr) };

  if (h == INVALID_HANDLE_VALUE) {
    throw std::system_error(::GetLastError(),
                            std::system_category(),
                            "Failed to touch file: " + path.string());
  }

  // Flush file buffers to ensure file metadata is committed to disk
  // before other processes try to read it. Critical for multi-process
  // cache synchronization on Windows.
  if (!::FlushFileBuffers(h)) {
    ::CloseHandle(h);
    throw std::system_error(::GetLastError(),
                            std::system_category(),
                            "Failed to flush file buffers: " + path.string());
  }

  ::CloseHandle(h);

  // Flush parent directory to ensure file is immediately visible to other processes
  std::filesystem::path const parent{ path.parent_path() };
  if (!parent.empty()) { flush_directory(parent); }
}

void flush_directory(std::filesystem::path const &dir) {
  HANDLE const dir_h{ ::CreateFileW(dir.c_str(),
                                    GENERIC_READ,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    nullptr,
                                    OPEN_EXISTING,
                                    FILE_FLAG_BACKUP_SEMANTICS,
                                    nullptr) };

  if (dir_h != INVALID_HANDLE_VALUE) {
    ::FlushFileBuffers(dir_h);
    ::CloseHandle(dir_h);
  }
}

bool file_exists(std::filesystem::path const &path) {
  // On Windows, std::filesystem::exists() uses cached directory listings that aren't
  // invalidated by FlushFileBuffers. To bypass the cache, we directly attempt to open
  // the file - this forces Windows to check the actual filesystem.
  HANDLE const h{ ::CreateFileW(path.c_str(),
                                GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr,
                                OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL,
                                nullptr) };

  if (h != INVALID_HANDLE_VALUE) {
    ::CloseHandle(h);
    return true;
  }

  return false;
}

[[noreturn]] void terminate_process() { ::TerminateProcess(::GetCurrentProcess(), 1); }

bool is_tty() { return ::_isatty(::_fileno(stderr)) != 0; }

platform_id native() { return platform_id::WINDOWS; }

std::string_view os_name() { return "windows"; }

std::string_view arch_name() {
#if defined(_M_ARM64)
  return "arm64";
#elif defined(_M_X64)
  return "x86_64";
#else
#error "unsupported architecture"
#endif
}

std::error_code remove_all_with_retry(std::filesystem::path const &target) {
  // Windows antivirus (Defender) and Search indexer often hold file handles
  // briefly after files are created/downloaded. Retry with exponential backoff.
  constexpr int kMaxRetries{ 8 };
  constexpr int kInitialDelayMs{ 50 };
  constexpr int kMaxDelayMs{ 1000 };

  std::error_code ec;
  for (int attempt{ 0 }; attempt < kMaxRetries; ++attempt) {
    std::filesystem::remove_all(target, ec);
    if (!ec) { return ec; }

    // ERROR_SHARING_VIOLATION (32) and ERROR_LOCK_VIOLATION (33) are the
    // typical errors when another process has the file/directory open.
    // Also handle ERROR_ACCESS_DENIED (5) which can occur during AV scans.
    DWORD const win_err{ static_cast<DWORD>(ec.value()) };
    bool const retryable{ win_err == ERROR_SHARING_VIOLATION ||
                          win_err == ERROR_LOCK_VIOLATION ||
                          win_err == ERROR_ACCESS_DENIED };
    if (!retryable) { break; }

    if (attempt + 1 < kMaxRetries) {
      // Exponential backoff: 50, 100, 200, 400, 800, 1000, 1000ms (~3.5s total)
      int delay_ms{ kInitialDelayMs << attempt };
      if (delay_ms > kMaxDelayMs) { delay_ms = kMaxDelayMs; }
      ::Sleep(static_cast<DWORD>(delay_ms));
    }
  }

  return ec;
}

std::filesystem::path expand_path(std::string_view p) {
  if (p.empty()) { return {}; }

  std::string result;
  size_t i{ 0 };

  // Leading ~ → USERPROFILE
  if (p[0] == '~' && (p.size() == 1 || p[1] == '/' || p[1] == '\\')) {
    char const *home{ std::getenv("USERPROFILE") };
    if (!home) { throw std::runtime_error("USERPROFILE not set for tilde expansion"); }
    result = home;
    i = 1;
  }

  while (i < p.size()) {
    if (p[i] == '$') {
      ++i;
      bool const braced{ i < p.size() && p[i] == '{' };
      if (braced) { ++i; }

      size_t const start{ i };
      while (i < p.size() &&
             (std::isalnum(static_cast<unsigned char>(p[i])) || p[i] == '_')) {
        ++i;
      }

      std::string var_name{ p.substr(start, i - start) };
      if (braced && i < p.size() && p[i] == '}') { ++i; }

      if (char const *val{ std::getenv(var_name.c_str()) }) {
        result += val;
      } else if (var_name == "HOME") {
        // $HOME is common in cross-platform scripts; map to USERPROFILE on Windows
        if (char const *profile{ std::getenv("USERPROFILE") }) { result += profile; }
      }
      // other undefined vars → empty string on Windows
    } else {
      result += p[i++];
    }
  }

  return result;
}

std::vector<std::string> get_environment() {
  std::vector<std::string> result;
  if (char *block{ GetEnvironmentStringsA() }; block) {
    for (char const *p = block; *p; p += std::strlen(p) + 1) { result.emplace_back(p); }
    FreeEnvironmentStringsA(block);
  }
  return result;
}

namespace {

// Build a flat command line string from argv with proper Windows quoting.
std::string build_cmdline(char **argv) {
  std::string cmdline;
  for (int i = 0; argv[i]; ++i) {
    if (i > 0) { cmdline += ' '; }
    std::string_view arg{ argv[i] };
    bool const needs_quote{ arg.empty() ||
                            arg.find_first_of(" \t\"") != std::string_view::npos };
    if (!needs_quote) {
      cmdline += arg;
      continue;
    }

    cmdline += '"';
    for (auto it = arg.begin();;) {
      int n_bs{ 0 };
      while (it != arg.end() && *it == '\\') {
        ++it;
        ++n_bs;
      }

      if (it == arg.end()) {
        cmdline.append(static_cast<size_t>(n_bs * 2), '\\');
        break;
      }

      if (*it == '"') {
        cmdline.append(static_cast<size_t>(n_bs * 2 + 1), '\\');
      } else {
        cmdline.append(static_cast<size_t>(n_bs), '\\');
      }
      cmdline += *it++;
    }
    cmdline += '"';
  }
  return cmdline;
}

// Build a double-null-terminated environment block for CreateProcessA.
std::string build_env_block(std::vector<std::string> const &env) {
  std::string block;
  for (auto const &e : env) {
    block.append(e);
    block.push_back('\0');
  }
  block.push_back('\0');  // double-null terminator
  return block;
}

}  // namespace

int exec_process(std::filesystem::path const &binary,
                 char **argv,
                 std::vector<std::string> env) {
  auto cmdline{ build_cmdline(argv) };
  auto env_block{ build_env_block(env) };

  STARTUPINFOA si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};

  if (!CreateProcessA(binary.string().c_str(),
                      cmdline.data(),
                      nullptr,
                      nullptr,
                      TRUE,
                      0,
                      env_block.data(),
                      nullptr,
                      &si,
                      &pi)) {
    throw std::runtime_error("exec_process: CreateProcess failed: " +
                             std::to_string(GetLastError()));
  }

  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD exit_code{};
  GetExitCodeProcess(pi.hProcess, &exit_code);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);

  return static_cast<int>(exit_code);
}

}  // namespace envy::platform

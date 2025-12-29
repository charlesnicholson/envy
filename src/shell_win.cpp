#if !defined(_WIN32)
#error "shell_win.cpp should only be compiled on Windows builds"
#else

#include "shell.h"

#include "platform.h"
#include "util.h"

#include <algorithm>
#include <array>
#include <cwchar>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <type_traits>
#include <variant>
#include <vector>

namespace envy {
namespace {

constexpr DWORD kPipeBufferSize{ 4096 };
constexpr size_t kLinePendingReserve{ 256 };

HANDLE g_job_object{ NULL };

class handle_closer {
 public:
  handle_closer() = default;
  explicit handle_closer(HANDLE handle) : handle_{ handle } {}
  ~handle_closer() {
    if (handle_) { ::CloseHandle(handle_); }
  }

  handle_closer(handle_closer const &) = delete;
  handle_closer &operator=(handle_closer const &) = delete;

  HANDLE get() const { return handle_; }
  HANDLE release() {
    HANDLE tmp{ handle_ };
    handle_ = nullptr;
    return tmp;
  }
  void reset(HANDLE handle = nullptr) {
    if (handle_ && handle_ != handle) { ::CloseHandle(handle_); }
    handle_ = handle;
  }

 private:
  HANDLE handle_{ nullptr };
};

std::wstring utf8_to_wstring(std::string_view input) {
  if (input.empty()) { return {}; }
  // Use MB_ERR_INVALID_CHARS to detect malformed UTF-8; fall back to permissive mode on
  // error
  int required{ ::MultiByteToWideChar(CP_UTF8,
                                      MB_ERR_INVALID_CHARS,
                                      input.data(),
                                      static_cast<int>(input.size()),
                                      nullptr,
                                      0) };
  if (required == 0) {
    DWORD const err{ ::GetLastError() };
    if (err == ERROR_NO_UNICODE_TRANSLATION) {
      // Invalid UTF-8 sequence; retry without strict validation (replaces with U+FFFD)
      required = ::MultiByteToWideChar(CP_UTF8,
                                       0,
                                       input.data(),
                                       static_cast<int>(input.size()),
                                       nullptr,
                                       0);
      if (required == 0) {
        DWORD const err2{ ::GetLastError() };
        // Distinguish Unicode errors from other failures (buffer size, etc.)
        if (err2 == ERROR_NO_UNICODE_TRANSLATION) {
          throw std::system_error(
              err2,
              std::system_category(),
              "MultiByteToWideChar (permissive): invalid Unicode translation");
        } else {
          throw std::system_error(err2,
                                  std::system_category(),
                                  "MultiByteToWideChar (permissive)");
        }
      }
    } else {
      throw std::system_error(err, std::system_category(), "MultiByteToWideChar");
    }
  }
  std::wstring result;
  result.resize(static_cast<size_t>(required));
  int const converted{ ::MultiByteToWideChar(CP_UTF8,
                                             0,
                                             input.data(),
                                             static_cast<int>(input.size()),
                                             result.data(),
                                             required) };
  if (converted == 0) {
    throw std::system_error(::GetLastError(),
                            std::system_category(),
                            "MultiByteToWideChar");
  }
  return result;
}

std::string wstring_to_utf8(std::wstring_view input) {
  if (input.empty()) { return {}; }
  // WC_ERR_INVALID_CHARS fails on unpaired surrogates; use permissive mode (flag 0)
  // which replaces unmappable chars with U+FFFD or default char
  int const required{ ::WideCharToMultiByte(CP_UTF8,
                                            0,
                                            input.data(),
                                            static_cast<int>(input.size()),
                                            nullptr,
                                            0,
                                            nullptr,
                                            nullptr) };
  if (required == 0) {
    // Conversion error; pipe output may contain invalid wide chars (rare)
    // Return empty string rather than crash - caller will get truncated output
    DWORD const err{ ::GetLastError() };
    if (err == ERROR_NO_UNICODE_TRANSLATION) { return {}; }
    throw std::system_error(err, std::system_category(), "WideCharToMultiByte");
  }
  std::string result;
  result.resize(static_cast<size_t>(required));
  int const converted{ ::WideCharToMultiByte(CP_UTF8,
                                             0,
                                             input.data(),
                                             static_cast<int>(input.size()),
                                             result.data(),
                                             required,
                                             nullptr,
                                             nullptr) };
  if (converted == 0) {
    throw std::system_error(::GetLastError(),
                            std::system_category(),
                            "WideCharToMultiByte");
  }
  return result;
}

bool ends_with_crlf(std::wstring const &text) {
  return text.size() >= 2 && text[text.size() - 2] == L'\r' &&
         text[text.size() - 1] == L'\n';
}

std::wstring normalize_newlines(std::wstring_view input) {
  std::wstring result{};
  result.reserve(input.size() + 2);
  for (size_t i{ 0 }; i < input.size(); ++i) {
    wchar_t const ch{ input[i] };
    if (ch == L'\r') {
      result.push_back(L'\r');
      if (i + 1 < input.size() && input[i + 1] == L'\n') {
        result.push_back(L'\n');
        ++i;
      } else {
        result.push_back(L'\n');
      }
    } else if (ch == L'\n') {
      result.push_back(L'\r');
      result.push_back(L'\n');
    } else {
      result.push_back(ch);
    }
  }
  return result;
}

// Build script contents for PowerShell only (wide). For cmd we emit narrow text later.
std::wstring build_powershell_script_contents(std::string_view script) {
  std::wstring user_script{ normalize_newlines(utf8_to_wstring(script)) };

  // Execute user script, check for errors, preserve stdout/stderr and exit code
  std::wstring wrapper;
  wrapper.reserve(user_script.size() + 128);
  wrapper.append(L"$ErrorActionPreference = 'Continue'\r\n");
  wrapper.append(L"$Error.Clear()\r\n");  // Clear previous errors
  wrapper.append(user_script);
  // Only add newline if script doesn't already end with one
  if (!user_script.empty() && user_script.back() != L'\n') {
    wrapper.append(L"\r\n");
  }
  // Exit with external command exit code if set, else 1 if error occurred, else 0
  wrapper.append(L"if ($LASTEXITCODE) { exit $LASTEXITCODE }\r\n");
  wrapper.append(L"if ($Error.Count -gt 0) { exit 1 }\r\n");
  wrapper.append(L"exit 0\r\n");
  return wrapper;
}

std::filesystem::path create_temp_script(std::string_view script,
                                         shell_run_cfg const &inv) {
  wchar_t temp_dir[MAX_PATH + 1];
  DWORD const dir_len{ ::GetTempPathW(MAX_PATH, temp_dir) };
  if (dir_len == 0 || dir_len > MAX_PATH) {
    throw std::system_error(::GetLastError(),
                            std::system_category(),
                            "GetTempPathW failed");
  }

  // Generate unique filename directly without GetTempFileNameW to avoid zero-byte file
  // creation that can trigger sharing violations from antivirus/indexers
  DWORD const pid{ ::GetCurrentProcessId() };
  ULONGLONG const tick{ ::GetTickCount64() };

  // Determine extension based on shell type
  std::wstring ext{ std::visit(
      match{
          [](shell_choice const &shell_cfg) -> std::wstring {
            return shell_cfg == shell_choice::powershell ? L".ps1" : L".cmd";
          },
          [](custom_shell_file const &shell_cfg) -> std::wstring {
            return utf8_to_wstring(shell_cfg.ext);
          },
          [](custom_shell_inline const &) -> std::wstring {
            return L".tmp";  // Generic extension for inline mode temp files
          },
      },
      inv.shell) };

  std::wstring const filename{ L"env" + std::to_wstring(pid) + L"_" +
                               std::to_wstring(tick) + ext };
  std::filesystem::path script_path{ std::wstring{ temp_dir } + filename };

  // Create file with retry on sharing violation
  HANDLE file{ INVALID_HANDLE_VALUE };
  for (int retry{ 0 }; retry < 3; ++retry) {
    file = ::CreateFileW(script_path.c_str(),
                         GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_DELETE,
                         nullptr,
                         CREATE_NEW,
                         FILE_ATTRIBUTE_NORMAL,
                         nullptr);
    if (file != INVALID_HANDLE_VALUE) { break; }

    DWORD const err{ ::GetLastError() };
    if ((err == ERROR_SHARING_VIOLATION || err == ERROR_FILE_EXISTS) && retry < 2) {
      ::Sleep(10);  // Brief wait before retry
      continue;
    }
    throw std::system_error(err, std::system_category(), "CreateFileW failed");
  }
  handle_closer file_guard{ file };

  DWORD written{ 0 };

  std::visit(
      match{
          [&](shell_choice const &shell_cfg) {
            if (shell_cfg == shell_choice::powershell) {
              // UTF-16 BOM + UTF-16 LE content
              std::wstring const content{ build_powershell_script_contents(script) };
              wchar_t const bom{ 0xFEFF };
              if (!::WriteFile(file_guard.get(), &bom, sizeof(bom), &written, nullptr) ||
                  written != sizeof(bom)) {
                throw std::system_error(::GetLastError(),
                                        std::system_category(),
                                        "WriteFile failed");
              }
              if (!content.empty()) {
                DWORD const byte_count{ static_cast<DWORD>(content.size() *
                                                           sizeof(wchar_t)) };
                if (!::WriteFile(file_guard.get(),
                                 content.data(),
                                 byte_count,
                                 &written,
                                 nullptr) ||
                    written != byte_count) {
                  throw std::system_error(::GetLastError(),
                                          std::system_category(),
                                          "WriteFile failed");
                }
              }
            } else {  // cmd
              // cmd.exe UTF-8 support: Windows 10 build 17134+ supports UTF-8 (CP_UTF8)
              // natively. Older versions use system codepage (CP1252, CP932, etc.) which
              // breaks non-ASCII. This implementation requires Windows 10+; non-ASCII on
              // older versions will fail.
              std::string narrow{ script };
              std::string normalized{};
              normalized.reserve(narrow.size() + 8);
              for (size_t i = 0; i < narrow.size(); ++i) {
                char ch = narrow[i];
                if (ch == '\r') {
                  normalized.push_back('\r');
                  if (i + 1 < narrow.size() && narrow[i + 1] == '\n') {
                    normalized.push_back('\n');
                    ++i;
                  } else {
                    normalized.push_back('\n');
                  }
                } else if (ch == '\n') {
                  normalized.push_back('\r');
                  normalized.push_back('\n');
                } else {
                  normalized.push_back(ch);
                }
              }
              if (!normalized.empty() &&
                  (normalized.size() < 2 ||
                   normalized.substr(normalized.size() - 2) != "\r\n")) {
                normalized.append("\r\n");
              }
              if (!normalized.empty()) {
                DWORD const byte_count{ static_cast<DWORD>(normalized.size()) };
                if (!::WriteFile(file_guard.get(),
                                 normalized.data(),
                                 byte_count,
                                 &written,
                                 nullptr) ||
                    written != byte_count) {
                  throw std::system_error(::GetLastError(),
                                          std::system_category(),
                                          "WriteFile failed");
                }
              }
            }
          },
          [&](custom_shell_file const &) {
            // Write UTF-8 without BOM for custom shells
            std::string content{ script };
            if (!content.empty()) {
              DWORD const byte_count{ static_cast<DWORD>(content.size()) };
              if (!::WriteFile(file_guard.get(),
                               content.data(),
                               byte_count,
                               &written,
                               nullptr) ||
                  written != byte_count) {
                throw std::system_error(::GetLastError(),
                                        std::system_category(),
                                        "WriteFile failed");
              }
            }
          },
          [&](custom_shell_inline const &) {
            // Write UTF-8 without BOM for custom shells
            std::string content{ script };
            if (!content.empty()) {
              DWORD const byte_count{ static_cast<DWORD>(content.size()) };
              if (!::WriteFile(file_guard.get(),
                               content.data(),
                               byte_count,
                               &written,
                               nullptr) ||
                  written != byte_count) {
                throw std::system_error(::GetLastError(),
                                        std::system_category(),
                                        "WriteFile failed");
              }
            }
          },
      },
      inv.shell);

  if (!::FlushFileBuffers(file_guard.get())) {
    throw std::system_error(::GetLastError(),
                            std::system_category(),
                            "FlushFileBuffers failed");
  }

  return script_path;
}

std::vector<wchar_t> build_environment_block(shell_env_t const &env) {
  // Inherit parent when no overrides.
  if (env.empty()) { return {}; }

  // Merge parent + overrides (Windows env vars are case-insensitive).
  shell_env_t merged{ shell_getenv() };
  for (auto const &[override_key, override_value] : env) {
    // Find and replace existing entry case-insensitively
    auto it{ std::find_if(merged.begin(),
                          merged.end(),
                          [&override_key](auto const &entry) {
                            return ::_stricmp(entry.first.c_str(), override_key.c_str()) ==
                                   0;
                          }) };

    if (it != merged.end()) {
      // Replace existing entry (preserve override's case for key)
      merged.erase(it);
    }
    merged[override_key] = override_value;
  }

  std::vector<wchar_t> block{};
  for (auto const &[key, value] : merged) {
    std::wstring wkey{ utf8_to_wstring(key) };
    std::wstring wvalue{ utf8_to_wstring(value) };
    block.insert(block.end(), wkey.begin(), wkey.end());
    block.push_back(L'=');
    block.insert(block.end(), wvalue.begin(), wvalue.end());
    block.push_back(L'\0');
  }
  block.push_back(L'\0');
  return block;
}

void stream_pipe_lines(HANDLE pipe,
                       shell_stream stream,
                       shell_run_cfg const &cfg) {
  std::string pending{};
  pending.reserve(kLinePendingReserve);
  std::array<char, kPipeBufferSize> buffer{};
  size_t offset{ 0 };

  while (true) {
    DWORD read_bytes{ 0 };
    BOOL const success{
      ::ReadFile(pipe, buffer.data(), buffer.size(), &read_bytes, nullptr)
    };
    if (!success) {
      DWORD const err = ::GetLastError();
      if (err == ERROR_BROKEN_PIPE) { break; }
      if (err == ERROR_HANDLE_EOF) { break; }
      throw std::system_error(err, std::system_category(), "ReadFile failed");
    }
    if (read_bytes == 0) { break; }

    pending.append(buffer.data(), read_bytes);

    size_t newline{ 0 };
    while ((newline = pending.find('\n', offset)) != std::string::npos) {
      std::string_view line{ pending.data() + offset, newline - offset };
      if (!line.empty() && line.back() == '\r') { line.remove_suffix(1); }
      if (stream == shell_stream::std_out) {
        if (cfg.on_stdout_line) { cfg.on_stdout_line(line); }
      } else {
        if (cfg.on_stderr_line) { cfg.on_stderr_line(line); }
      }
      if (cfg.on_output_line) { cfg.on_output_line(line); }
      offset = newline + 1;
    }

    // Compact buffer when offset grows large
    if (offset > kPipeBufferSize) {
      pending.erase(0, offset);
      offset = 0;
    }
  }

  if (offset < pending.size()) {
    std::string_view line{ pending.data() + offset, pending.size() - offset };
    if (!line.empty() && line.back() == '\r') { line.remove_suffix(1); }
    if (stream == shell_stream::std_out) {
      if (cfg.on_stdout_line) { cfg.on_stdout_line(line); }
    } else {
      if (cfg.on_stderr_line) { cfg.on_stderr_line(line); }
    }
    if (cfg.on_output_line) { cfg.on_output_line(line); }
  }
}

shell_result wait_for_child(HANDLE process) {
  DWORD const wait_result{ ::WaitForSingleObject(process, INFINITE) };
  if (wait_result != WAIT_OBJECT_0) {
    throw std::system_error(::GetLastError(),
                            std::system_category(),
                            "WaitForSingleObject failed");
  }

  DWORD exit_code{ 0 };
  if (!::GetExitCodeProcess(process, &exit_code)) {
    throw std::system_error(::GetLastError(),
                            std::system_category(),
                            "GetExitCodeProcess failed");
  }

  return { .exit_code = static_cast<int>(exit_code), .signal = std::nullopt };
}

std::wstring quote_arg(std::wstring_view arg) {
  // Windows command-line quoting: wrap in quotes if contains spaces or special chars
  if (arg.find_first_of(L" \t\"") == std::wstring_view::npos) {
    return std::wstring{ arg };
  }

  std::wstring result{ L"\"" };
  for (size_t i{ 0 }; i < arg.size(); ++i) {
    size_t backslash_count{ 0 };
    while (i < arg.size() && arg[i] == L'\\') {
      ++backslash_count;
      ++i;
    }

    if (i == arg.size()) {
      // Backslashes at end of string: double them before closing quote
      result.append(backslash_count * 2, L'\\');
      break;
    } else if (arg[i] == L'"') {
      // Backslashes before quote: double them, then escape the quote
      result.append(backslash_count * 2 + 1, L'\\');
      result.push_back(L'"');
    } else {
      // Normal backslashes: keep as-is
      result.append(backslash_count, L'\\');
      result.push_back(arg[i]);
    }
  }
  result.push_back(L'"');
  return result;
}

std::wstring build_command_line_builtin(shell_choice shell,
                                        std::filesystem::path const &script_path) {
  std::wstring quoted{ L"\"" };
  quoted.append(script_path.wstring());
  quoted.push_back(L'"');

  if (shell == shell_choice::powershell) {
    // -NoProfile: Skip user profile for consistent, fast startup (intentionally breaks
    // profile-dependent scripts)
    return L"powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass "
           L"-File " +
           quoted;
  }

  // cmd shell requires nested quotes: ""C:\path\script.cmd""
  std::wstring command{ L"cmd.exe /D /V:OFF /S /C \"" };
  command.append(quoted);
  command.push_back(L'"');
  return command;
}

std::wstring build_command_line_custom(custom_shell_file const &shell,
                                       std::filesystem::path const &script_path) {
  std::wstring command{};
  for (size_t i{ 0 }; i < shell.argv.size(); ++i) {
    if (i > 0) { command.push_back(L' '); }
    command.append(quote_arg(utf8_to_wstring(shell.argv[i])));
  }
  // Append script path as final argument
  command.push_back(L' ');
  command.append(quote_arg(script_path.wstring()));
  return command;
}

std::wstring build_command_line_custom(custom_shell_inline const &shell,
                                       std::string_view script_content) {
  std::wstring command{};
  for (size_t i{ 0 }; i < shell.argv.size(); ++i) {
    if (i > 0) { command.push_back(L' '); }
    command.append(quote_arg(utf8_to_wstring(shell.argv[i])));
  }
  // Append script content as final argument
  command.push_back(L' ');
  command.append(quote_arg(utf8_to_wstring(std::string{ script_content })));
  return command;
}

}  // namespace

void shell_init() {
  g_job_object = ::CreateJobObjectW(nullptr, nullptr);
  if (!g_job_object) {
    throw std::system_error(::GetLastError(),
                            std::system_category(),
                            "Failed to create job object for child process management");
  }

  JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
  info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;

  if (!::SetInformationJobObject(g_job_object,
                                 JobObjectExtendedLimitInformation,
                                 &info,
                                 sizeof(info))) {
    DWORD const err{ ::GetLastError() };
    ::CloseHandle(g_job_object);
    g_job_object = NULL;
    throw std::system_error(err,
                            std::system_category(),
                            "Failed to configure job object");
  }
}

shell_env_t shell_getenv() {
  shell_env_t env{};
  LPWCH block{ ::GetEnvironmentStringsW() };
  if (!block) { return env; }

  auto const free_block = [](LPWCH ptr) { ::FreeEnvironmentStringsW(ptr); };
  std::unique_ptr<wchar_t, decltype(free_block)> guard{ block, free_block };

  for (wchar_t const *entry{ block }; *entry != L'\0'; entry += wcslen(entry) + 1) {
    std::wstring_view const view{ entry };
    size_t const sep{ view.find(L'=') };
    if (sep == std::wstring_view::npos || sep == 0) { continue; }
    std::wstring const key{ view.substr(0, sep) };
    std::wstring const value{ view.substr(sep + 1) };
    env[wstring_to_utf8(key)] = wstring_to_utf8(value);
  }

  return env;
}

shell_result shell_run(std::string_view script, shell_run_cfg const &cfg) {
  std::filesystem::path const script_path{ create_temp_script(script, cfg) };
  scoped_path_cleanup cleanup{ script_path };

  // Environment block must be mutable for CreateProcessW (LPVOID), build then keep
  // non-const.
  std::vector<wchar_t> env_block{ build_environment_block(cfg.env) };

  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.lpSecurityDescriptor = nullptr;
  sa.bInheritHandle = TRUE;

  HANDLE stdout_read{ nullptr };
  HANDLE stdout_write{ nullptr };
  if (!::CreatePipe(&stdout_read, &stdout_write, &sa, 0)) {
    throw std::system_error(::GetLastError(), std::system_category(), "CreatePipe failed");
  }

  handle_closer stdout_read_end{ stdout_read };
  handle_closer stdout_write_end{ stdout_write };
  if (!::SetHandleInformation(stdout_read_end.get(), HANDLE_FLAG_INHERIT, 0)) {
    throw std::system_error(::GetLastError(),
                            std::system_category(),
                            "SetHandleInformation failed");
  }

  HANDLE stderr_read{ nullptr };
  HANDLE stderr_write{ nullptr };
  if (!::CreatePipe(&stderr_read, &stderr_write, &sa, 0)) {
    throw std::system_error(::GetLastError(), std::system_category(), "CreatePipe failed");
  }

  handle_closer stderr_read_end{ stderr_read };
  handle_closer stderr_write_end{ stderr_write };
  if (!::SetHandleInformation(stderr_read_end.get(), HANDLE_FLAG_INHERIT, 0)) {
    throw std::system_error(::GetLastError(),
                            std::system_category(),
                            "SetHandleInformation failed");
  }

  HANDLE const null_in{ ::CreateFileW(L"NUL",
                                      GENERIC_READ,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE,
                                      nullptr,
                                      OPEN_EXISTING,
                                      FILE_ATTRIBUTE_NORMAL,
                                      nullptr) };
  if (null_in == INVALID_HANDLE_VALUE) {
    throw std::system_error(::GetLastError(),
                            std::system_category(),
                            "CreateFileW NUL failed");
  }
  handle_closer stdin_handle{ null_in };
  if (!::SetHandleInformation(stdin_handle.get(),
                              HANDLE_FLAG_INHERIT,
                              HANDLE_FLAG_INHERIT)) {
    throw std::system_error(::GetLastError(),
                            std::system_category(),
                            "SetHandleInformation failed");
  }

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags |= STARTF_USESTDHANDLES;
  si.hStdInput = stdin_handle.get();
  si.hStdOutput = stdout_write_end.get();
  si.hStdError = stderr_write_end.get();

  PROCESS_INFORMATION pi{};

  std::wstring const command_line{ std::visit(
      match{
          [&script_path](shell_choice const &shell_cfg) -> std::wstring {
            return build_command_line_builtin(shell_cfg, script_path);
          },
          [&script_path](custom_shell_file const &shell_cfg) -> std::wstring {
            return build_command_line_custom(shell_cfg, script_path);
          },
          [&script](custom_shell_inline const &shell_cfg) -> std::wstring {
            return build_command_line_custom(shell_cfg, script);
          },
      },
      cfg.shell) };
  std::vector<wchar_t> cmd_buffer{ command_line.begin(), command_line.end() };
  cmd_buffer.push_back(L'\0');

  std::wstring cwd_storage{};
  wchar_t *cwd_ptr{ nullptr };
  if (cfg.cwd) {
    cwd_storage = cfg.cwd->wstring();
    cwd_ptr = cwd_storage.data();
  }

  BOOL const created{ ::CreateProcessW(nullptr,
                                       cmd_buffer.data(),
                                       nullptr,
                                       nullptr,
                                       TRUE,
                                       CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW,
                                       env_block.empty() ? nullptr : env_block.data(),
                                       cwd_ptr,
                                       &si,
                                       &pi) };
  if (!created) {
    throw std::system_error(::GetLastError(),
                            std::system_category(),
                            "CreateProcessW failed");
  }

  // Add process to job object to ensure child dies when envy terminates
  if (g_job_object && !::AssignProcessToJobObject(g_job_object, pi.hProcess)) {
    // Log warning but continue - non-fatal, worst case is orphaned child on Ctrl+C
    // (This should never fail in practice unless job was closed or process already exited)
  }

  handle_closer process{ pi.hProcess };
  handle_closer thread{ pi.hThread };

  // Parent no longer needs these handles
  stdin_handle.reset();
  stdout_write_end.reset();
  stderr_write_end.reset();

  shell_result result{};
  std::exception_ptr stdout_exception;
  std::exception_ptr stderr_exception;

  try {
    std::thread stdout_reader{ [&]() {
      try {
        stream_pipe_lines(stdout_read_end.get(), shell_stream::std_out, cfg);
      } catch (...) {
        stdout_exception = std::current_exception();
      }
    } };

    std::thread stderr_reader{ [&]() {
      try {
        stream_pipe_lines(stderr_read_end.get(), shell_stream::std_err, cfg);
      } catch (...) {
        stderr_exception = std::current_exception();
      }
    } };

    stdout_reader.join();
    stderr_reader.join();

    if (stdout_exception) { std::rethrow_exception(stdout_exception); }
    if (stderr_exception) { std::rethrow_exception(stderr_exception); }

    result = wait_for_child(process.get());
  } catch (...) {
    ::TerminateProcess(process.get(), 1);
    ::WaitForSingleObject(process.get(), INFINITE);
    throw;
  }

  return result;
}

}  // namespace envy

#endif  // _WIN32

#if !defined(_WIN32)
#error "shell_win32.cpp should only be compiled on Windows builds"
#else

#include "shell.h"

#include "platform_windows.h"
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
#include <vector>

namespace envy {
namespace {

constexpr DWORD kPipeBufferSize{ 4096 };
constexpr size_t kLinePendingReserve{ 256 };

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
  int const required{ ::MultiByteToWideChar(CP_UTF8,
                                            0,
                                            input.data(),
                                            static_cast<int>(input.size()),
                                            nullptr,
                                            0) };
  if (required == 0) {
    throw std::system_error(::GetLastError(),
                            std::system_category(),
                            "MultiByteToWideChar");
  }
  std::wstring result{ static_cast<size_t>(required), L'\0' };
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
  int const required{ ::WideCharToMultiByte(CP_UTF8,
                                            0,
                                            input.data(),
                                            static_cast<int>(input.size()),
                                            nullptr,
                                            0,
                                            nullptr,
                                            nullptr) };
  if (required == 0) {
    throw std::system_error(::GetLastError(),
                            std::system_category(),
                            "WideCharToMultiByte");
  }
  std::string result{ static_cast<size_t>(required), '\0' };
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

std::wstring build_script_contents(std::string_view script, shell_run_cfg const &inv) {
  std::wstring const normalized{ normalize_newlines(utf8_to_wstring(script)) };
  if (!normalized.empty() && !ends_with_crlf(normalized)) { normalized.append(L"\r\n"); }

  std::wstring content{};
  if (inv.shell == shell_choice::powershell) {
    content.append(normalized);
  } else {
    content.append(L"@echo off\r\n");
    content.append(L"setlocal EnableExtensions\r\n");
    content.append(normalized);
  }

  if (!content.empty() && !ends_with_crlf(content)) { content.append(L"\r\n"); }
  return content;
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

  wchar_t temp_file[MAX_PATH + 1];
  if (::GetTempFileNameW(temp_dir, L"env", 0, temp_file) == 0) {
    throw std::system_error(::GetLastError(),
                            std::system_category(),
                            "GetTempFileNameW failed");
  }

  std::filesystem::path const base_path{ temp_file };
  std::filesystem::path script_path{ base_path };
  if (inv.shell == shell_choice::powershell) {
    script_path.replace_extension(".ps1");
  } else {
    script_path.replace_extension(".cmd");
  }

  std::error_code rename_ec{};
  std::filesystem::rename(base_path, script_path, rename_ec);
  if (rename_ec) {
    // Fall back to removing the original temp file and using the renamed path
    std::filesystem::remove(base_path, rename_ec);
  }

  HANDLE const file{ ::CreateFileW(script_path.c_str(),
                                   GENERIC_WRITE,
                                   FILE_SHARE_READ,
                                   nullptr,
                                   CREATE_ALWAYS,
                                   FILE_ATTRIBUTE_NORMAL,
                                   nullptr) };
  if (file == INVALID_HANDLE_VALUE) {
    throw std::system_error(::GetLastError(),
                            std::system_category(),
                            "CreateFileW failed");
  }
  handle_closer file_guard{ file };

  std::wstring const content{ build_script_contents(script, inv) };
  wchar_t const bom{ 0xFEFF };
  DWORD written{ 0 };
  if (!::WriteFile(file_guard.get(), &bom, sizeof(bom), &written, nullptr) ||
      written != sizeof(bom)) {
    throw std::system_error(::GetLastError(), std::system_category(), "WriteFile failed");
  }

  if (!content.empty()) {
    DWORD const byte_count{ static_cast<DWORD>(content.size() * sizeof(wchar_t)) };
    if (!::WriteFile(file_guard.get(), content.data(), byte_count, &written, nullptr) ||
        written != byte_count) {
      throw std::system_error(::GetLastError(),
                              std::system_category(),
                              "WriteFile failed");
    }
  }

  if (!::FlushFileBuffers(file_guard.get())) {
    throw std::system_error(::GetLastError(),
                            std::system_category(),
                            "FlushFileBuffers failed");
  }

  return script_path;
}

std::vector<wchar_t> build_environment_block(shell_env_t const &env) {
  if (env.empty()) { return { L'\0' }; }

  std::vector<std::pair<std::string, std::string> > entries{ env.begin(), env.end() };
  std::sort(entries.begin(), entries.end(), [](auto const &a, auto const &b) {
    return a.first < b.first;
  });

  std::vector<wchar_t> block{};
  for (auto const &[key, value] : entries) {
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
                       std::function<void(std::string_view)> const &callback) {
  std::string pending{};
  pending.reserve(kLinePendingReserve);
  std::array<char, kPipeBufferSize> buffer{};

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
    while ((newline = pending.find('\n')) != std::string::npos) {
      std::string line{ pending.substr(0, newline) };
      if (!line.empty() && line.back() == '\r') { line.pop_back(); }
      callback(line);
      pending.erase(0, newline + 1);
    }
  }

  if (!pending.empty()) {
    if (!pending.empty() && pending.back() == '\r') { pending.pop_back(); }
    callback(pending);
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

std::wstring build_command_line(shell_choice shell,
                                std::filesystem::path const &script_path) {
  std::wstring quoted{ L"\"" };
  quoted.append(script_path.wstring());
  quoted.push_back(L'"');

  if (shell == shell_choice::powershell) {
    return L"powershell.exe -NoLogo -NonInteractive -ExecutionPolicy Bypass -File " +
           quoted;
  }

  // cmd shell requires nested quotes: ""C:\path\script.cmd""
  std::wstring command{ L"cmd.exe /D /V:OFF /S /C \"" };
  command.append(quoted);
  command.push_back(L'"');
  return command;
}

}  // namespace

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
  if (!cfg.on_output_line) {
    throw std::invalid_argument("shell_run: on_output_line callback must be set");
  }

  std::filesystem::path const script_path{ create_temp_script(script, cfg) };
  scoped_path_cleanup cleanup{ script_path };

  std::vector<wchar_t> const env_block{ build_environment_block(cfg.env) };

  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.lpSecurityDescriptor = nullptr;
  sa.bInheritHandle = TRUE;

  HANDLE stdout_read{ nullptr };
  HANDLE stdout_write{ nullptr };
  if (!::CreatePipe(&stdout_read, &stdout_write, &sa, 0)) {
    throw std::system_error(::GetLastError(), std::system_category(), "CreatePipe failed");
  }

  handle_closer read_end{ stdout_read };
  handle_closer write_end{ stdout_write };
  if (!::SetHandleInformation(read_end.get(), HANDLE_FLAG_INHERIT, 0)) {
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
  si.hStdOutput = write_end.get();
  si.hStdError = write_end.get();

  PROCESS_INFORMATION pi{};

  std::wstring const command_line{ build_command_line(cfg.shell, script_path) };
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

  handle_closer process{ pi.hProcess };
  handle_closer thread{ pi.hThread };

  // Parent no longer needs these handles
  stdin_handle.reset();
  write_end.reset();

  shell_result result{};
  try {
    stream_pipe_lines(read_end.get(), cfg.on_output_line);
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

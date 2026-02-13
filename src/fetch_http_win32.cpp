#if defined(_WIN32)

#include "fetch_http.h"

#include "uri.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <WinInet.h>

#include <cstdio>
#include <fstream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>

namespace envy {

namespace {

constexpr char kDefaultUserAgent[]{ "envy-fetch/0.0" };
constexpr DWORD kReadBufferSize{ 65536 };
constexpr DWORD kCommonFlags{ INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE |
                              INTERNET_FLAG_KEEP_CONNECTION };

std::string win_error_message(DWORD error_code) {
  char *buf{ nullptr };

  // Try wininet.dll first for WinINet-specific error messages, then system.
  DWORD len{ FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                                FORMAT_MESSAGE_FROM_SYSTEM |
                                FORMAT_MESSAGE_FROM_HMODULE |
                                FORMAT_MESSAGE_IGNORE_INSERTS,
                            GetModuleHandleA("wininet.dll"),
                            error_code,
                            0,
                            reinterpret_cast<LPSTR>(&buf),
                            0,
                            nullptr) };
  if (!len || !buf) {
    len = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                             FORMAT_MESSAGE_FROM_SYSTEM |
                             FORMAT_MESSAGE_IGNORE_INSERTS,
                         nullptr,
                         error_code,
                         0,
                         reinterpret_cast<LPSTR>(&buf),
                         0,
                         nullptr);
  }
  if (!len || !buf) {
    if (buf) { LocalFree(buf); }
    char code_buf[64];
    snprintf(code_buf, sizeof(code_buf), "error code %lu",
             static_cast<unsigned long>(error_code));
    return code_buf;
  }

  // Strip trailing \r\n from FormatMessage output.
  while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) { --len; }
  std::string msg{ buf, len };
  LocalFree(buf);
  return msg;
}

[[noreturn]] void throw_wininet_error(char const *context) {
  DWORD const err{ GetLastError() };
  throw std::runtime_error(std::string(context) + ": " + win_error_message(err));
}

// Process-wide WinInet session.  Created once on first download; Windows
// reclaims the handle at process exit.  Sharing a single session avoids
// repeated proxy auto-detection (WPAD) that serializes concurrent downloads.
std::once_flag g_session_once;
HINTERNET g_session{ nullptr };

HINTERNET ensure_session() {
  std::call_once(g_session_once, [] {
    g_session = InternetOpenA(kDefaultUserAgent,
                              INTERNET_OPEN_TYPE_PRECONFIG,
                              nullptr, nullptr, 0);
    if (!g_session) { throw_wininet_error("InternetOpen failed"); }
  });
  return g_session;
}

struct internet_handle_deleter {
  void operator()(HINTERNET h) const {
    if (h) { InternetCloseHandle(h); }
  }
};
using internet_handle = std::unique_ptr<void, internet_handle_deleter>;

std::optional<std::uint64_t> query_content_length(HINTERNET request) {
  char buf[32]{};
  DWORD buf_len{ sizeof(buf) };
  DWORD header_index{ 0 };
  if (HttpQueryInfoA(request, HTTP_QUERY_CONTENT_LENGTH, buf, &buf_len, &header_index)) {
    char *end{ nullptr };
    auto const val{ std::strtoull(buf, &end, 10) };
    if (end != buf && *end == '\0') { return val; }
  }
  return std::nullopt;
}

void check_http_status(HINTERNET request) {
  DWORD status_code{ 0 };
  DWORD size{ sizeof(status_code) };
  DWORD header_index{ 0 };
  if (HttpQueryInfoA(request,
                     HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
                     &status_code,
                     &size,
                     &header_index)) {
    if (status_code >= 400) {
      char msg[128];
      snprintf(msg, sizeof(msg), "HTTP error %lu",
               static_cast<unsigned long>(status_code));
      throw std::runtime_error(msg);
    }
  }
}

void read_response_to_file(HINTERNET request,
                           std::ofstream &output,
                           std::filesystem::path const &dest,
                           fetch_progress_cb_t const &progress,
                           std::optional<std::uint64_t> content_length) {
  char buffer[kReadBufferSize];
  std::uint64_t bytes_read_total{ 0 };

  for (;;) {
    DWORD bytes_read{ 0 };
    if (!InternetReadFile(request, buffer, sizeof(buffer), &bytes_read)) {
      std::error_code ec;
      output.close();
      std::filesystem::remove(dest, ec);
      throw_wininet_error("InternetReadFile failed");
    }
    if (bytes_read == 0) { break; }

    output.write(buffer, static_cast<std::streamsize>(bytes_read));
    if (!output) {
      output.close();
      std::error_code ec;
      std::filesystem::remove(dest, ec);
      throw std::runtime_error("fetch_http_download: failed to write to destination file");
    }

    bytes_read_total += bytes_read;

    if (progress) {
      bool const should_continue{ progress(fetch_progress_t{
          std::in_place_type<fetch_transfer_progress>,
          fetch_transfer_progress{ .transferred = bytes_read_total,
                                   .total = content_length } }) };
      if (!should_continue) {
        output.close();
        std::error_code ec;
        std::filesystem::remove(dest, ec);
        throw std::runtime_error("fetch_http_download: transfer aborted by progress callback");
      }
    }
  }
}

std::filesystem::path download_with_post(
    std::string_view url,
    std::filesystem::path const &resolved_destination,
    std::ofstream &output,
    fetch_progress_cb_t const &progress,
    std::string const &post_body,
    HINTERNET session) {
  // Parse URL components for InternetConnect + HttpOpenRequest
  URL_COMPONENTSA uc{};
  uc.dwStructSize = sizeof(uc);
  char host[256]{};
  char path[2048]{};
  uc.lpszHostName = host;
  uc.dwHostNameLength = sizeof(host);
  uc.lpszUrlPath = path;
  uc.dwUrlPathLength = sizeof(path);

  std::string url_str{ url };
  if (!InternetCrackUrlA(url_str.c_str(), static_cast<DWORD>(url_str.size()), 0, &uc)) {
    throw_wininet_error("InternetCrackUrl failed (URL may exceed buffer capacity)");
  }

  DWORD flags{ kCommonFlags };
  if (uc.nScheme == INTERNET_SCHEME_HTTPS) { flags |= INTERNET_FLAG_SECURE; }

  internet_handle connection{ InternetConnectA(session,
                                               host,
                                               uc.nPort,
                                               nullptr,
                                               nullptr,
                                               INTERNET_SERVICE_HTTP,
                                               0,
                                               0) };
  if (!connection) { throw_wininet_error("InternetConnect failed"); }

  internet_handle request{ HttpOpenRequestA(connection.get(),
                                            "POST",
                                            path,
                                            nullptr,
                                            nullptr,
                                            nullptr,
                                            flags,
                                            0) };
  if (!request) { throw_wininet_error("HttpOpenRequest failed"); }

  // Kick the TUI before the blocking send so the user sees immediate progress.
  if (progress) {
    progress(fetch_progress_t{ std::in_place_type<fetch_transfer_progress>,
                               fetch_transfer_progress{ .transferred = 0,
                                                        .total = std::nullopt } });
  }

  char const *content_type{ "Content-Type: application/x-www-form-urlencoded\r\n" };
  if (!HttpSendRequestA(request.get(),
                        content_type,
                        static_cast<DWORD>(strlen(content_type)),
                        const_cast<char *>(post_body.c_str()),  // NOLINT: HttpSendRequestA takes non-const LPVOID but doesn't modify it
                        static_cast<DWORD>(post_body.size()))) {
    throw_wininet_error("HttpSendRequest failed");
  }

  check_http_status(request.get());
  auto const content_length{ query_content_length(request.get()) };
  read_response_to_file(request.get(), output, resolved_destination, progress,
                        content_length);

  output.flush();
  if (!output) {
    output.close();
    std::error_code ec;
    std::filesystem::remove(resolved_destination, ec);
    throw std::runtime_error("fetch_http_download: failed to flush destination file");
  }
  output.close();

  return resolved_destination;
}

}  // namespace

std::filesystem::path fetch_http_download(
    std::string_view url,
    std::filesystem::path const &destination,
    fetch_progress_cb_t const &progress,
    std::optional<std::string> const &post_data) {
  if (destination.empty()) {
    throw std::invalid_argument("fetch_http_download: destination is empty");
  }

  std::filesystem::path resolved_destination{ destination };
  if (!resolved_destination.is_absolute()) {
    resolved_destination = std::filesystem::absolute(resolved_destination);
  }
  resolved_destination = resolved_destination.lexically_normal();

  std::error_code ec;
  auto const parent{ resolved_destination.parent_path() };
  if (!parent.empty()) {
    std::filesystem::create_directories(parent, ec);
    if (ec) {
      throw std::runtime_error(
          "fetch_http_download: failed to create parent directory: " +
          parent.string() + ": " + ec.message());
    }
  }

  std::ofstream output{ resolved_destination, std::ios::binary | std::ios::trunc };
  if (!output.is_open()) {
    throw std::runtime_error(
        "fetch_http_download: failed to open destination: " +
        resolved_destination.string());
  }

  HINTERNET session{ ensure_session() };

  // POST requires InternetConnect + HttpOpenRequest + HttpSendRequest
  if (post_data && uri_is_http_scheme(url)) {
    return download_with_post(url, resolved_destination, output, progress,
                              *post_data, session);
  }

  // GET / FTP — use InternetOpenUrl (handles redirects automatically)
  // Kick the TUI immediately so the user sees progress before the blocking
  // DNS + TLS handshake inside InternetOpenUrlA.
  if (progress) {
    progress(fetch_progress_t{ std::in_place_type<fetch_transfer_progress>,
                               fetch_transfer_progress{ .transferred = 0,
                                                        .total = std::nullopt } });
  }

  std::string url_str{ url };
  DWORD flags{ kCommonFlags };
  if (uri_is_https_scheme(url)) { flags |= INTERNET_FLAG_SECURE; }

  internet_handle request{ InternetOpenUrlA(session,
                                            url_str.c_str(),
                                            nullptr,
                                            0,
                                            flags,
                                            0) };
  if (!request) {
    output.close();
    std::filesystem::remove(resolved_destination, ec);
    throw_wininet_error("InternetOpenUrl failed");
  }

  // Check HTTP status for HTTP(S) URLs; FTP doesn't have HTTP status codes
  if (uri_is_http_scheme(url)) { check_http_status(request.get()); }

  auto const content_length{ uri_is_http_scheme(url)
                                 ? query_content_length(request.get())
                                 : std::nullopt };

  read_response_to_file(request.get(), output, resolved_destination, progress,
                        content_length);

  output.flush();
  if (!output) {
    output.close();
    std::filesystem::remove(resolved_destination, ec);
    throw std::runtime_error("fetch_http_download: failed to flush destination file");
  }
  output.close();

  return resolved_destination;
}

}  // namespace envy

#endif  // defined(_WIN32)

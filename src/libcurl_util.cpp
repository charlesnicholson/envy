#include "libcurl_util.h"

#include <curl/curl.h>

#include <fstream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>

namespace envy {

namespace {

constexpr char kDefaultUserAgent[]{ "envy-fetch/0.0" };

size_t curl_write_file(char *ptr, size_t size, size_t nmemb, void *userdata) {
  auto *stream{ static_cast<std::ofstream *>(userdata) };
  size_t const total{ size * nmemb };
  stream->write(ptr, static_cast<std::streamsize>(total));
  if (!*stream) { return 0; }
  return total;
}

}  // namespace

void libcurl_ensure_initialized() {
  static std::once_flag once;
  std::call_once(once, [] {
    CURLcode const code{ curl_global_init(CURL_GLOBAL_DEFAULT) };
    if (code != CURLE_OK) {
      throw std::runtime_error(std::string("curl_global_init failed: ") +
                               curl_easy_strerror(code));
    }
  });
}

std::filesystem::path libcurl_download(std::string_view url,
                                       std::filesystem::path const &destination,
                                       fetch_progress_cb_t const &progress) {
  (void)progress;  // Progress integration pending.
  libcurl_ensure_initialized();

  std::string const url_copy{ url };

  if (destination.empty()) { throw std::invalid_argument("libcurl_download: destination is empty"); }

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
      throw std::runtime_error("libcurl_download: failed to create parent directory: " +
                               parent.string() + ": " + ec.message());
    }
  }

  std::ofstream output{ resolved_destination, std::ios::binary | std::ios::trunc };
  if (!output.is_open()) {
    throw std::runtime_error("libcurl_download: failed to open destination: " +
                             resolved_destination.string());
  }

  std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> handle{ curl_easy_init(),
                                                              &curl_easy_cleanup };
  if (!handle) { throw std::runtime_error("curl_easy_init failed"); }

  auto const setopt = [handle = handle.get()](auto option, auto value) {
    CURLcode const rc{ curl_easy_setopt(handle, option, value) };
    if (rc != CURLE_OK) {
      throw std::runtime_error(std::string("curl_easy_setopt failed: ") +
                               curl_easy_strerror(rc));
    }
  };

  setopt(CURLOPT_URL, url_copy.c_str());
  setopt(CURLOPT_FOLLOWLOCATION, 1L);
  setopt(CURLOPT_FAILONERROR, 1L);
  setopt(CURLOPT_NOSIGNAL, 1L);
  setopt(CURLOPT_USERAGENT, kDefaultUserAgent);
  setopt(CURLOPT_WRITEFUNCTION, curl_write_file);
  setopt(CURLOPT_WRITEDATA, &output);
  setopt(CURLOPT_NOPROGRESS, 1L);  // Progress handling will be wired in later.

  CURLcode const perform_result{ curl_easy_perform(handle.get()) };
  if (perform_result != CURLE_OK) {
    output.close();
    std::filesystem::remove(resolved_destination, ec);
    throw std::runtime_error(std::string("curl_easy_perform failed: ") +
                             curl_easy_strerror(perform_result));
  }

  output.flush();
  if (!output) {
    output.close();
    std::filesystem::remove(resolved_destination, ec);
    throw std::runtime_error("libcurl_download: failed to flush destination file");
  }
  output.close();

  return resolved_destination;
}

}  // namespace envy


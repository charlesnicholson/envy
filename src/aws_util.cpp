#include "aws_util.h"
#include "platform.h"

#include "aws/core/Aws.h"
#include "aws/core/client/ClientConfiguration.h"
#include "aws/core/utils/logging/LogLevel.h"
#include "aws/core/utils/logging/NullLogSystem.h"
#include "aws/core/utils/threading/PooledThreadExecutor.h"
#include "aws/s3/S3Client.h"
#include "aws/transfer/TransferHandle.h"
#include "aws/transfer/TransferManager.h"

#include <cstdlib>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>

namespace envy {
namespace {

constexpr char const *kAllocationTag{ "envy-aws-util" };

Aws::SDKOptions g_options;
std::once_flag g_init_once;
std::mutex g_state_mutex;
bool g_initialized{ false };

// Per-region TransferManager cache.
struct transfer_context {
  std::shared_ptr<Aws::S3::S3Client> client;
  std::shared_ptr<Aws::Transfer::TransferManager> manager;
};

std::mutex g_transfer_mutex;
std::unordered_map<std::string, transfer_context> g_transfer_contexts;

struct progress_entry {  // Per-download progress state, keyed by destination file path.
  fetch_progress_cb_t cb;
  std::uint64_t last_reported{ 0 };
  bool cancelled{ false };
};

std::mutex g_progress_mutex;
std::unordered_map<std::string, progress_entry> g_progress_map;

constexpr std::uint64_t kProgressInterval{ 1 << 17 };  // 128 KB

void configure_options(Aws::SDKOptions &options) {
  options.loggingOptions.logLevel = Aws::Utils::Logging::LogLevel::Off;
  options.loggingOptions.logger_create_fn = [] {
    return Aws::MakeShared<Aws::Utils::Logging::NullLogSystem>(kAllocationTag);
  };
}

void on_download_progress(
    Aws::Transfer::TransferManager const *,
    std::shared_ptr<Aws::Transfer::TransferHandle const> const &handle) {
  auto const dest{ std::string(handle->GetTargetFilePath().c_str()) };
  auto const transferred{ handle->GetBytesTransferred() };
  auto const total{ handle->GetBytesTotalSize() };

  fetch_progress_cb_t cb;
  {
    std::lock_guard<std::mutex> lock{ g_progress_mutex };
    auto const it{ g_progress_map.find(dest) };
    if (it == g_progress_map.end()) { return; }
    if (transferred - it->second.last_reported < kProgressInterval) { return; }
    it->second.last_reported = transferred;
    cb = it->second.cb;
  }

  std::optional<std::uint64_t> content_length;
  if (total > 0) { content_length = total; }
  fetch_progress_t payload{ std::in_place_type<fetch_transfer_progress>,
                            fetch_transfer_progress{ transferred, content_length } };
  if (!cb(payload)) {
    std::lock_guard<std::mutex> lock{ g_progress_mutex };
    auto const it{ g_progress_map.find(dest) };
    if (it != g_progress_map.end()) { it->second.cancelled = true; }
  }
}

transfer_context &get_transfer_context(std::string const &region) {
  std::lock_guard<std::mutex> lock{ g_transfer_mutex };
  auto const it{ g_transfer_contexts.find(region) };
  if (it != g_transfer_contexts.end()) { return it->second; }

  Aws::Client::ClientConfiguration config;
  if (!region.empty()) { config.region = Aws::String(region.c_str()); }
  auto client{ Aws::MakeShared<Aws::S3::S3Client>(kAllocationTag, config) };

  Aws::Transfer::TransferManagerConfiguration tm_config(nullptr);
  tm_config.s3Client = client;
  tm_config.downloadProgressCallback = on_download_progress;
  tm_config.executorCreateFn = [] {
    return Aws::MakeShared<Aws::Utils::Threading::PooledThreadExecutor>(kAllocationTag, 8);
  };
  auto manager{ Aws::Transfer::TransferManager::Create(tm_config) };

  auto [inserted, _] = g_transfer_contexts.emplace(
      region,
      transfer_context{ std::move(client), std::move(manager) });
  return inserted->second;
}

struct s3_uri_parts {
  std::string bucket;
  std::string key;
};

s3_uri_parts parse_s3_uri(std::string_view uri) {
  constexpr std::string_view kPrefix{ "s3://" };
  if (!uri.starts_with(kPrefix)) {
    throw std::invalid_argument("aws_s3_download: URI must start with s3://");
  }

  std::string_view remainder{ uri.substr(kPrefix.size()) };
  auto const slash{ remainder.find('/') };
  if (slash == std::string_view::npos || slash == 0 || slash + 1 >= remainder.size()) {
    throw std::invalid_argument("aws_s3_download: URI must include bucket and key");
  }

  return s3_uri_parts{ .bucket = std::string(remainder.substr(0, slash)),
                       .key = std::string(remainder.substr(slash + 1)) };
}

}  // namespace

void aws_init() {
  std::call_once(g_init_once, [] {
    platform::env_var_set("AWS_SDK_LOAD_CONFIG", "1");
    configure_options(g_options);
    Aws::InitAPI(g_options);
    std::lock_guard<std::mutex> lock{ g_state_mutex };
    g_initialized = true;
  });
}

void aws_shutdown() {
  std::lock_guard<std::mutex> lock{ g_state_mutex };
  if (!g_initialized) { return; }
  {
    std::lock_guard<std::mutex> tl{ g_transfer_mutex };
    g_transfer_contexts.clear();
  }
  g_initialized = false;
  Aws::ShutdownAPI(g_options);
}

aws_shutdown_guard::~aws_shutdown_guard() { aws_shutdown(); }

std::filesystem::path aws_s3_download(s3_download_request const &request) {
  if (request.destination.empty()) {
    throw std::invalid_argument("aws_s3_download: destination path is empty");
  }

  std::filesystem::path resolved_destination{ request.destination };
  if (!resolved_destination.is_absolute()) {
    resolved_destination = std::filesystem::absolute(resolved_destination);
  }
  resolved_destination = resolved_destination.lexically_normal();

  auto const parts{ parse_s3_uri(request.uri) };

  auto const parent{ resolved_destination.parent_path() };
  if (!parent.empty()) {
    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
    if (ec) {
      throw std::runtime_error(
          "aws_s3_download: failed to create destination directories: " + ec.message());
    }
  }

  aws_init();

  std::string const region{ request.region.value_or("") };
  auto &ctx{ get_transfer_context(region) };

  std::string const dest_str{ resolved_destination.string() };

  if (request.progress) {  // Register per-download callback before starting the transfer.
    std::lock_guard<std::mutex> lock{ g_progress_mutex };
    g_progress_map.insert_or_assign(dest_str, progress_entry{ request.progress });
  }

  auto handle{ ctx.manager->DownloadFile(Aws::String(parts.bucket.c_str()),
                                         Aws::String(parts.key.c_str()),
                                         Aws::String(dest_str.c_str())) };

  handle->WaitUntilFinished();

  if (auto const was_cancelled{ [&] {  // Deregister callback and check cancellation.
        std::lock_guard<std::mutex> lock{ g_progress_mutex };
        auto const it{ g_progress_map.find(dest_str) };
        if (it == g_progress_map.end()) { return false; }
        bool const c{ it->second.cancelled };
        g_progress_map.erase(it);
        return c;
      }() }) {
    handle->Cancel();
    throw std::runtime_error("aws_s3_download: transfer cancelled by progress callback");
  }

  auto const status{ handle->GetStatus() };
  if (status == Aws::Transfer::TransferStatus::FAILED ||
      status == Aws::Transfer::TransferStatus::CANCELED ||
      status == Aws::Transfer::TransferStatus::ABORTED) {
    auto const &error{ handle->GetLastError() };
    auto const http_code{ static_cast<int>(error.GetResponseCode()) };
    std::string msg{ "aws_s3_download: transfer failed" };

    if (auto const &name{ error.GetExceptionName() }; !name.empty()) {
      msg += ": " + std::string(name.c_str());
    }

    auto const &body{ error.GetMessage() };
    if (!body.empty() && body != "No response body.") {
      msg += ": " + std::string(body.c_str());
    }

    if (http_code > 0) { msg += " (HTTP " + std::to_string(http_code) + ")"; }

    if (http_code == 401 || http_code == 403) {
      msg +=
          "\n  Hint: AWS credentials may be missing or expired."
          " Run 'aws sso login' or check your AWS configuration.";
    }

    throw std::runtime_error(msg);
  }

  if (request.progress) {  // Final progress callback so the bar reaches 100%.
    auto const transferred{ handle->GetBytesTransferred() };
    auto const total{ handle->GetBytesTotalSize() };
    std::optional<std::uint64_t> content_length;
    if (total > 0) { content_length = total; }
    fetch_progress_t payload{ std::in_place_type<fetch_transfer_progress>,
                              fetch_transfer_progress{ transferred, content_length } };
    request.progress(payload);
  }

  return resolved_destination;
}

}  // namespace envy

#include "aws_util.h"
#include "platform.h"

#include "aws/core/Aws.h"
#include "aws/core/client/ClientConfiguration.h"
#include "aws/core/utils/logging/LogLevel.h"
#include "aws/core/utils/logging/NullLogSystem.h"
#include "aws/s3/S3Client.h"
#include "aws/s3/model/GetObjectRequest.h"

#include <array>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace envy {
namespace {

constexpr char const *kAllocationTag{ "envy-aws-util" };

Aws::SDKOptions g_options;
std::once_flag g_init_once;
std::mutex g_state_mutex;
bool g_initialized{ false };

void configure_options(Aws::SDKOptions &options) {
  options.loggingOptions.logLevel = Aws::Utils::Logging::LogLevel::Off;
  options.loggingOptions.logger_create_fn = [] {
    return Aws::MakeShared<Aws::Utils::Logging::NullLogSystem>(kAllocationTag);
  };
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

  return s3_uri_parts{
    .bucket = std::string(remainder.substr(0, slash)),
    .key = std::string(remainder.substr(slash + 1)),
  };
}

}  // namespace

void aws_init() {
  std::call_once(g_init_once, [] {
    platform::set_env_var("AWS_SDK_LOAD_CONFIG", "1");
    configure_options(g_options);
    Aws::InitAPI(g_options);
    std::lock_guard<std::mutex> lock{ g_state_mutex };
    g_initialized = true;
  });
}

void aws_shutdown() {
  std::lock_guard<std::mutex> lock{ g_state_mutex };
  if (!g_initialized) { return; }
  g_initialized = false;
  Aws::ShutdownAPI(g_options);
}

aws_shutdown_guard::~aws_shutdown_guard() { aws_shutdown(); }

void aws_s3_download(s3_download_request const &request) {
  aws_init();

  if (request.destination.empty()) {
    throw std::invalid_argument("aws_s3_download: destination path is empty");
  }

  auto const parts{ parse_s3_uri(request.uri) };

  Aws::Client::ClientConfiguration config;
  if (request.region && !request.region->empty()) {
    config.region = Aws::String(request.region->c_str());
  }
  Aws::S3::S3Client s3_client{ config };

  Aws::S3::Model::GetObjectRequest get_request;
  get_request.SetBucket(Aws::String(parts.bucket.c_str()));
  get_request.SetKey(Aws::String(parts.key.c_str()));

  auto outcome{ s3_client.GetObject(get_request) };
  if (!outcome.IsSuccess()) {
    auto const &error{ outcome.GetError() };
    throw std::runtime_error(std::string("aws_s3_download: GetObject failed: ") +
                             error.GetExceptionName().c_str() + " - " +
                             error.GetMessage().c_str());
  }

  auto const parent{ request.destination.parent_path() };
  if (!parent.empty()) {
    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
    if (ec) {
      throw std::runtime_error(
          "aws_s3_download: failed to create destination directories: " + ec.message());
    }
  }

  auto result = outcome.GetResultWithOwnership();
  auto &body_stream = result.GetBody();

  std::ofstream output{ request.destination, std::ios::binary | std::ios::trunc };
  if (!output.is_open()) {
    throw std::runtime_error("aws_s3_download: failed to open destination file");
  }

  constexpr std::size_t kBufferSize{ 1 << 16 };
  std::array<char, kBufferSize> buffer{};
  std::uint64_t written{ 0 };
  std::optional<std::uint64_t> total_bytes;
  if (auto const len{ result.GetContentLength() }; len >= 0) {
    total_bytes = static_cast<std::uint64_t>(len);
  }

  while (body_stream) {
    body_stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    auto const count = static_cast<std::size_t>(body_stream.gcount());
    if (count == 0) { break; }

    output.write(buffer.data(), static_cast<std::streamsize>(count));
    if (!output) {
      throw std::runtime_error("aws_s3_download: failed to write destination file");
    }

    written += count;
    if (request.progress) {
      fetch_transfer_progress transfer{ written, total_bytes };
      fetch_progress_t payload{ std::in_place_type<fetch_transfer_progress>, transfer };
      if (!request.progress(payload)) {
        throw std::runtime_error("aws_s3_download: aborted by progress callback");
      }
    }
  }
}

}  // namespace envy

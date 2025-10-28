#include "sha256.h"

#include "mbedtls/sha256.h"

#include <cstdio>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

struct file_closer {
  void operator()(std::FILE *file) const noexcept {
    if (file) { static_cast<void>(std::fclose(file)); }
  }
};

}  // namespace

namespace envy {

std::array<unsigned char, 32> sha256(std::filesystem::path const &file_path) {
  if (!std::filesystem::exists(file_path)) {
    throw std::runtime_error("sha256: file does not exist: " + file_path.string());
  }

  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);

  std::unique_ptr<decltype(ctx), decltype(&mbedtls_sha256_free)> ctx_scope(
      &ctx,
      &mbedtls_sha256_free);

  if (mbedtls_sha256_starts(&ctx, 0)) {
    throw std::runtime_error("sha256: mbedtls_sha256_starts failed");
  }

#if defined(_WIN32)
  std::FILE *raw_file{ _wfopen(file_path.c_str(), L"rb") };
#else
  std::FILE *raw_file{ std::fopen(file_path.c_str(), "rb") };
#endif

  if (!raw_file) {
    throw std::runtime_error("sha256: failed to open file: " + file_path.string());
  }

  std::unique_ptr<std::FILE, file_closer> file(raw_file);

  std::vector<unsigned char> buffer(1024 * 1024);
  while (true) {
    auto const read_bytes{
      std::fread(buffer.data(), sizeof(unsigned char), buffer.size(), file.get())
    };

    if (read_bytes > 0) {
      if (mbedtls_sha256_update(&ctx, buffer.data(), read_bytes)) {
        throw std::runtime_error("sha256: mbedtls_sha256_update failed");
      }
    }

    if (read_bytes < buffer.size()) {
      if (std::ferror(file.get())) { throw std::runtime_error("sha256: fread failed"); }
      break;
    }
  }

  std::array<unsigned char, 32> digest{};
  if (mbedtls_sha256_finish(&ctx, digest.data())) {
    throw std::runtime_error("sha256: mbedtls_sha256_finish failed");
  }

  return digest;
}

}  // namespace envy

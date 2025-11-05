#include "sha256.h"

#include "mbedtls/sha256.h"

#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct file_closer {
  void operator()(std::FILE *file) const noexcept {
    if (file) { static_cast<void>(std::fclose(file)); }
  }
};

}  // namespace

namespace envy {

sha256_t sha256(std::filesystem::path const &file_path) {
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

  sha256_t digest{};
  if (mbedtls_sha256_finish(&ctx, digest.data())) {
    throw std::runtime_error("sha256: mbedtls_sha256_finish failed");
  }

  return digest;
}

void sha256_verify(std::string const &expected_hex, sha256_t const &actual_hash) {
  if (expected_hex.size() != 64) {
    throw std::runtime_error(
        "sha256_verify: expected hex string must be 64 characters, got " +
        std::to_string(expected_hex.size()));
  }

  sha256_t expected_bytes{};
  for (size_t i = 0; i < 32; ++i) {
    char const hi = expected_hex[i * 2];
    char const lo = expected_hex[i * 2 + 1];

    auto constexpr nibble = [](char c) -> unsigned char {
      if (c >= '0' && c <= '9') return static_cast<unsigned char>(c - '0');
      if (c >= 'a' && c <= 'f') return static_cast<unsigned char>(c - 'a' + 10);
      if (c >= 'A' && c <= 'F') return static_cast<unsigned char>(c - 'A' + 10);
      throw std::runtime_error(std::string("sha256_verify: invalid hex character: ") + c);
    };

    expected_bytes[i] = static_cast<unsigned char>((nibble(hi) << 4) | nibble(lo));
  }

  if (expected_bytes != actual_hash) {
    auto to_hex = [](sha256_t const &bytes) -> std::string {
      std::string result;
      result.reserve(64);
      static char constexpr hex_chars[] = "0123456789abcdef";
      for (auto const byte : bytes) {
        result += hex_chars[(byte >> 4) & 0xf];
        result += hex_chars[byte & 0xf];
      }
      return result;
    };

    throw std::runtime_error("SHA256 mismatch: expected " + expected_hex + " but got " +
                             to_hex(actual_hash));
  }
}

}  // namespace envy

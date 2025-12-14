#include "util.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#include <string>
#endif

namespace envy {

std::string util_bytes_to_hex(void const *data, size_t length) {
  static constexpr char hex_chars[] = "0123456789abcdef";

  auto const bytes = static_cast<unsigned char const *>(data);
  std::string result;
  result.reserve(length * 2);

  for (size_t i{}; i < length; ++i) {
    result += hex_chars[(bytes[i] >> 4) & 0xf];
    result += hex_chars[bytes[i] & 0xf];
  }

  return result;
}

int util_hex_char_to_int(char c) {
  if (c >= '0' && c <= '9') { return c - '0'; }
  if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
  if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
  return -1;
}

std::vector<unsigned char> util_hex_to_bytes(std::string const &hex) {
  if (hex.size() % 2 != 0) {
    throw std::runtime_error("util_hex_to_bytes: hex string must have even length, got " +
                             std::to_string(hex.size()));
  }

  std::vector<unsigned char> result;
  result.reserve(hex.size() / 2);

  for (size_t i{}; i < hex.size(); i += 2) {
    int const hi{ util_hex_char_to_int(hex[i]) };
    int const lo{ util_hex_char_to_int(hex[i + 1]) };

    if (hi < 0) {
      throw std::runtime_error(
          std::string("util_hex_to_bytes: invalid character at position ") +
          std::to_string(i));
    }
    if (lo < 0) {
      throw std::runtime_error(
          std::string("util_hex_to_bytes: invalid character at position ") +
          std::to_string(i + 1));
    }

    result.push_back(static_cast<unsigned char>((hi << 4) | lo));
  }

  return result;
}

void file_deleter::operator()(std::FILE *file) const noexcept {
  if (file) { static_cast<void>(std::fclose(file)); }
}

file_ptr_t util_open_file(std::filesystem::path const &path, char const *mode) {
#if defined(_WIN32)
  // Convert mode string to wide string for _wfopen
  std::wstring wide_mode;
  wide_mode.reserve(std::strlen(mode));
  for (char const *p{ mode }; *p != '\0'; ++p) {
    wide_mode.push_back(static_cast<wchar_t>(*p));
  }
  return file_ptr_t{ _wfopen(path.c_str(), wide_mode.c_str()) };
#else
  return file_ptr_t{ std::fopen(path.c_str(), mode) };
#endif
}

std::vector<unsigned char> util_load_file(std::filesystem::path const &path) {
  auto file{ util_open_file(path, "rb") };
  if (!file) {
    throw std::runtime_error("util_load_file: failed to open file: " + path.string());
  }

  // Get file size
  if (std::fseek(file.get(), 0, SEEK_END) != 0) {
    throw std::runtime_error("util_load_file: failed to seek to end: " + path.string());
  }

  long const file_size{ std::ftell(file.get()) };
  if (file_size < 0) {
    throw std::runtime_error("util_load_file: failed to get file size: " + path.string());
  }

  if (std::fseek(file.get(), 0, SEEK_SET) != 0) {
    throw std::runtime_error("util_load_file: failed to seek to start: " + path.string());
  }

  // Read entire file
  std::vector<unsigned char> buffer(static_cast<size_t>(file_size));
  if (file_size > 0) {
    size_t const bytes_read{ std::fread(buffer.data(), 1, buffer.size(), file.get()) };
    if (bytes_read != buffer.size()) {
      throw std::runtime_error("util_load_file: failed to read entire file: " +
                               path.string());
    }
  }

  return buffer;
}

std::string util_format_bytes(std::uint64_t bytes) {
  static constexpr std::array<char const *, 5> kUnits{ "B", "KB", "MB", "GB", "TB" };

  double value{ static_cast<double>(bytes) };
  std::size_t unit{ 0 };

  while (value >= 1024.0 && unit + 1 < kUnits.size()) {
    value /= 1024.0;
    ++unit;
  }

  if (unit == 0) { return std::to_string(static_cast<std::uint64_t>(value)) + "B"; }

  std::ostringstream oss;
  oss.setf(std::ios::fixed, std::ios::floatfield);
  oss << std::setprecision(2) << value << kUnits[unit];
  return oss.str();
}

scoped_path_cleanup::scoped_path_cleanup(std::filesystem::path path)
    : path_{ std::move(path) } {}

scoped_path_cleanup::~scoped_path_cleanup() { cleanup(); }

void scoped_path_cleanup::reset(std::filesystem::path path) {
  cleanup();
  path_ = std::move(path);
}

void scoped_path_cleanup::cleanup() {
  if (path_.empty()) { return; }
  std::error_code ec;
  std::filesystem::remove(path_, ec);
  path_.clear();
}

}  // namespace envy

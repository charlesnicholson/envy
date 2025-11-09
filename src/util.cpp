#include "util.h"

#include <cstdio>
#include <cstring>
#include <stdexcept>

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

}  // namespace envy

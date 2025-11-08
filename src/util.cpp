#include "util.h"

#include <stdexcept>

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

    if (hi < 0 || lo < 0) {
      throw std::runtime_error(
          std::string("util_hex_to_bytes: invalid character at position ") +
          std::to_string(i));
    }

    result.push_back(static_cast<unsigned char>((hi << 4) | lo));
  }

  return result;
}

}  // namespace envy

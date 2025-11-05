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

std::vector<unsigned char> util_hex_to_bytes(std::string const &hex) {
  if (hex.size() % 2 != 0) {
    throw std::runtime_error("util_hex_to_bytes: hex string must have even length, got " +
                             std::to_string(hex.size()));
  }

  auto constexpr nibble = [](char c) -> unsigned char {
    if (c >= '0' && c <= '9') return static_cast<unsigned char>(c - '0');
    if (c >= 'a' && c <= 'f') return static_cast<unsigned char>(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return static_cast<unsigned char>(c - 'A' + 10);
    throw std::runtime_error(std::string("util_hex_to_bytes: invalid character: ") + c);
  };

  std::vector<unsigned char> result;
  result.reserve(hex.size() / 2);

  for (size_t i{}; i < hex.size(); i += 2) {
    char const hi{ hex[i] };
    char const lo{ hex[i + 1] };
    result.push_back(static_cast<unsigned char>((nibble(hi) << 4) | nibble(lo)));
  }

  return result;
}

}  // namespace envy

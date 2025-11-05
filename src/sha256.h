#pragma once

#include <array>
#include <filesystem>
#include <string>

namespace envy {

using sha256_t = std::array<unsigned char, 32>;

sha256_t sha256(std::filesystem::path const &file_path);

// Verify SHA256 hash matches expected hex string (case-insensitive)
// Throws std::runtime_error with detailed message if mismatch
void sha256_verify(std::string const &expected_hex, sha256_t const &actual_hash);

}  // namespace envy

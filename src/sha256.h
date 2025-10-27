#pragma once

#include <array>
#include <filesystem>

namespace envy {
std::array<unsigned char, 32> sha256(std::filesystem::path const &file_path);
}  // namespace envy

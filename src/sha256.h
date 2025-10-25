#pragma once

#include <filesystem>
#include <cstddef>
#include <array>

namespace envy {

std::array<unsigned char, 32> sha256(std::filesystem::path const &file_path);

}  // namespace envy

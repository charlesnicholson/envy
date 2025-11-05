#pragma once

#include <array>
#include <cstddef>

namespace envy {

using blake3_t = std::array<unsigned char, 32>;
blake3_t blake3_hash(void const *data, size_t length);

}  // namespace envy

#include "blake3_util.h"

#include "blake3.h"

namespace envy {

blake3_t blake3_hash(void const *data, size_t length) {
  blake3_t digest;
  blake3_hasher hasher;

  blake3_hasher_init(&hasher);
  blake3_hasher_update(&hasher, data, length);
  blake3_hasher_finalize(&hasher, digest.data(), digest.size());
  return digest;
}

}  // namespace envy

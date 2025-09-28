#include <git2.h>
#include <curl/curl.h>
#include <archive.h>
#include <archive_entry.h>
#include <blake3.h>
#include <tbb/global_control.h>

#include <array>
#include <cstdint>

#include "lua.hpp"

extern "C" {
#include "ssh_api.h"
}

int main() {
    if (git_libgit2_init() < 0) {
        return 1;
    }
    git_libgit2_shutdown();

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        return 1;
    }
    curl_global_cleanup();

    struct ssh *session = nullptr;
    if (ssh_init(&session, 0, nullptr) != 0 || session == nullptr) {
        return 1;
    }
    ssh_free(session);

    lua_State *L = luaL_newstate();
    if (!L) {
        return 1;
    }
    lua_close(L);

    tbb::global_control control(tbb::global_control::max_allowed_parallelism, 2);

    archive *writer = archive_write_new();
    if (!writer) {
        return 1;
    }
    archive_write_free(writer);

    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    std::array<uint8_t, BLAKE3_OUT_LEN> digest{};
    blake3_hasher_finalize(&hasher, digest.data(), digest.size());

    return 0;
}

#include <git2.h>
#include <curl/curl.h>
#include <archive.h>
#include <archive_entry.h>
#include <blake3.h>
#include <tbb/global_control.h>

#include <curl/curlver.h>
#include <mbedtls/md5.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

#include "lua.hpp"

int main() {
    if (git_libgit2_init() < 0) {
        return 1;
    }
    git_libgit2_shutdown();

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        return 1;
    }
    const curl_version_info_data *info = curl_version_info(CURLVERSION_NOW);
    if (!info) {
        curl_global_cleanup();
        return 1;
    }
    if ((info->features & CURL_VERSION_SSL) == 0U) {
        curl_global_cleanup();
        return 1;
    }
    if (!info->libssh_version || info->libssh_version[0] == '\0') {
        curl_global_cleanup();
        return 1;
    }
    const std::string_view ssl_backend{info->ssl_version ? info->ssl_version : ""};
    if (ssl_backend.find("SecureTransport") == std::string_view::npos) {
        curl_global_cleanup();
        return 1;
    }
    curl_global_cleanup();

    const auto features = git_libgit2_features();
    if ((features & GIT_FEATURE_HTTPS) == 0 || (features & GIT_FEATURE_SSH) == 0) {
        return 1;
    }

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

    std::array<unsigned char, 16> md5{};
    static constexpr std::string_view md5_msg =
        "The quick brown fox jumps over the lazy dog";
    if (mbedtls_md5(reinterpret_cast<const unsigned char *>(md5_msg.data()),
                    md5_msg.size(), md5.data()) != 0) {
        return 1;
    }
    static constexpr unsigned char md5_expected[16] = {
        0x9e, 0x10, 0x7d, 0x9d, 0x37, 0x2b, 0xb6, 0x82,
        0x6b, 0xd8, 0x1d, 0x35, 0x42, 0xa4, 0x19, 0xd6};
    if (!std::equal(md5.begin(), md5.end(), std::begin(md5_expected))) {
        return 1;
    }

    return 0;
}

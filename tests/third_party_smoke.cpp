#include <git2.h>
#include <curl/curl.h>
#include <archive.h>
#include <archive_entry.h>
#include <blake3.h>
#include <tbb/global_control.h>

#include <curl/curlver.h>
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

    return 0;
}

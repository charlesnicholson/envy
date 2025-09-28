#include <git2.h>
#include <curl/curl.h>
#include <archive.h>
#include <archive_entry.h>
#include <blake3.h>
#include <tbb/parallel_for.h>
#include <tbb/task_arena.h>

#include <curl/curlver.h>

#include "lua.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
void check_git() {
    std::cout << "[libgit2] Initializing runtime..." << std::endl;
    if (git_libgit2_init() < 0) {
        throw std::runtime_error("libgit2 failed to initialize");
    }
    const auto features = git_libgit2_features();
    if ((features & GIT_FEATURE_HTTPS) == 0) {
        git_libgit2_shutdown();
        throw std::runtime_error("libgit2 HTTPS transport unavailable");
    }
    if ((features & GIT_FEATURE_SSH) == 0) {
        git_libgit2_shutdown();
        throw std::runtime_error("libgit2 SSH transport unavailable");
    }
    git_libgit2_shutdown();
    std::cout << "[libgit2] HTTPS and SSH transports are available." << std::endl;
}

void check_curl() {
    std::cout << "[libcurl] Global init and transport capability check..." << std::endl;
    CURLcode init_result = curl_global_init(CURL_GLOBAL_ALL);
    if (init_result != CURLE_OK) {
        throw std::runtime_error("libcurl global init failed");
    }
    CURL *curl = curl_easy_init();
    if (!curl) {
        curl_global_cleanup();
        throw std::runtime_error("libcurl easy handle creation failed");
    }
    curl_easy_setopt(curl, CURLOPT_NOPROXY, "*");
    const curl_version_info_data *info = curl_version_info(CURLVERSION_NOW);
    if (!info) {
        curl_easy_cleanup(curl);
        curl_global_cleanup();
        throw std::runtime_error("curl_version_info returned null");
    }
    if ((info->features & CURL_VERSION_SSL) == 0U) {
        curl_easy_cleanup(curl);
        curl_global_cleanup();
        throw std::runtime_error("libcurl built without TLS support");
    }
    if (!info->libssh_version || info->libssh_version[0] == '\0') {
        curl_easy_cleanup(curl);
        curl_global_cleanup();
        throw std::runtime_error("libcurl built without libssh2 support");
    }
    const std::string_view ssl_backend{info->ssl_version ? info->ssl_version : ""};
    if (ssl_backend.find("SecureTransport") == std::string_view::npos) {
        curl_easy_cleanup(curl);
        curl_global_cleanup();
        throw std::runtime_error("libcurl not using SecureTransport");
    }
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    std::cout << "[libcurl] TLS backend: "
              << (info->ssl_version ? info->ssl_version : "unknown")
              << "; libssh2: "
              << (info->libssh_version ? info->libssh_version : "none")
              << std::endl;
}

void check_lua() {
    std::cout << "[Lua] Spinning up interpreter..." << std::endl;
    lua_State *L = luaL_newstate();
    if (!L) {
        throw std::runtime_error("luaL_newstate failed");
    }
    luaL_openlibs(L);
    if (luaL_dostring(L, "return 2 + 2") != LUA_OK) {
        lua_close(L);
        throw std::runtime_error("lua execution failed");
    }
    if (!lua_isinteger(L, -1) || lua_tointeger(L, -1) != 4) {
        lua_pop(L, 1);
        lua_close(L);
        throw std::runtime_error("lua returned unexpected result");
    }
    lua_pop(L, 1);
    lua_close(L);
    std::cout << "[Lua] Interpreter opened, executed script, and shutdown cleanly." << std::endl;
}

void check_tbb() {
    std::cout << "[oneTBB] Running parallel_for smoke test..." << std::endl;
    std::array<int, 8> values{};
    tbb::task_arena arena;
    arena.execute([&] {
        tbb::parallel_for(std::size_t{0}, values.size(), [&](std::size_t idx) {
            values[idx] = static_cast<int>(idx * idx);
        });
    });
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (values[i] != static_cast<int>(i * i)) {
            throw std::runtime_error("oneTBB parallel_for produced unexpected data");
        }
    }
    std::cout << "[oneTBB] parallel_for produced expected results." << std::endl;
}

void check_libarchive() {
    std::cout << "[libarchive] Creating archive in-memory..." << std::endl;
    static constexpr std::string_view payload = "archive-smoke-test";

    archive *writer = archive_write_new();
    if (!writer) {
        throw std::runtime_error("libarchive writer allocation failed");
    }
    archive_write_set_format_pax_restricted(writer);
    archive_write_add_filter_none(writer);

    std::array<char, 512> buffer{};
    size_t written = 0;
    if (archive_write_open_memory(writer, buffer.data(), buffer.size(), &written) != ARCHIVE_OK) {
        archive_write_free(writer);
        throw std::runtime_error("libarchive open memory failed");
    }

    archive_entry *entry = archive_entry_new();
    if (!entry) {
        archive_write_close(writer);
        archive_write_free(writer);
        throw std::runtime_error("libarchive entry allocation failed");
    }
    archive_entry_set_pathname(entry, "payload.txt");
    archive_entry_set_filetype(entry, AE_IFREG);
    archive_entry_set_perm(entry, 0644);
    archive_entry_set_size(entry, payload.size());

    if (archive_write_header(writer, entry) != ARCHIVE_OK) {
        archive_entry_free(entry);
        archive_write_close(writer);
        archive_write_free(writer);
        throw std::runtime_error("libarchive write header failed");
    }

    if (archive_write_data(writer, payload.data(), payload.size()) != static_cast<ssize_t>(payload.size())) {
        archive_entry_free(entry);
        archive_write_close(writer);
        archive_write_free(writer);
        throw std::runtime_error("libarchive write data failed");
    }

    archive_entry_free(entry);
    archive_write_close(writer);
    archive_write_free(writer);
    std::cout << "[libarchive] Wrote and freed PAX archive successfully." << std::endl;
}

void check_blake3() {
    std::cout << "[BLAKE3] Hashing smoke payload..." << std::endl;
    static constexpr std::string_view message = "blake3-smoke";
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, message.data(), message.size());

    std::array<uint8_t, BLAKE3_OUT_LEN> digest{};
    blake3_hasher_finalize(&hasher, digest.data(), digest.size());

    if (std::all_of(digest.begin(), digest.end(), [](uint8_t byte) { return byte == 0; })) {
        throw std::runtime_error("BLAKE3 produced all-zero digest");
    }

    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, message.data(), message.size());
    std::array<uint8_t, BLAKE3_OUT_LEN> digest_second{};
    blake3_hasher_finalize(&hasher, digest_second.data(), digest_second.size());

    if (digest != digest_second) {
        throw std::runtime_error("BLAKE3 digests are not deterministic");
    }
    std::cout << "[BLAKE3] Digest computed and verified." << std::endl;
}
} // namespace

int main() {
    try {
        std::cout << "=== codex-cmake-test dependency probe ===" << std::endl;
        check_git();
        check_curl();
        check_lua();
        check_tbb();
        check_libarchive();
        check_blake3();
    } catch (const std::exception &ex) {
        std::cerr << "Initialization failed: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "All dependencies initialized successfully." << std::endl;
    return EXIT_SUCCESS;
}

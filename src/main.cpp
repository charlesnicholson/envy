#include <git2.h>
#include <curl/curl.h>
#include <archive.h>
#include <archive_entry.h>
#include <blake3.h>
#include <tbb/parallel_for.h>
#include <tbb/task_arena.h>

extern "C" {
#include "ssh_api.h"
}

#include "lua.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
void check_git() {
    if (git_libgit2_init() < 0) {
        throw std::runtime_error("libgit2 failed to initialize");
    }
    git_libgit2_shutdown();
}

void check_curl() {
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
    curl_easy_cleanup(curl);
    curl_global_cleanup();
}

void check_ssh() {
    struct ssh *session = nullptr;
    if (ssh_init(&session, 0, nullptr) != 0 || session == nullptr) {
        throw std::runtime_error("OpenSSH ssh_init failed");
    }
    ssh_free(session);
}

void check_lua() {
    lua_State *L = luaL_newstate();
    if (!L) {
        throw std::runtime_error("luaL_newstate failed");
    }
    luaL_openlibs(L);
    if (luaL_dostring(L, "return 2 + 2") != LUA_OK) {
        lua_close(L);
        throw std::runtime_error("lua execution failed");
    }
    lua_pop(L, 1);
    lua_close(L);
}

void check_tbb() {
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
}

void check_libarchive() {
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

}

void check_blake3() {
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
}
} // namespace

int main() {
    try {
        check_git();
        check_curl();
        check_ssh();
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

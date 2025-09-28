#include <git2.h>
#include <curl/curl.h>
#include <archive.h>
#include <archive_entry.h>
#include <blake3.h>
#include <tbb/parallel_for.h>
#include <tbb/task_arena.h>

#include <curl/curlver.h>
#include <openssl/evp.h>

#include "lua.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace {
std::string to_hex(const unsigned char *data, size_t length) {
  std::ostringstream oss;
  oss << std::hex << std::setfill('0');
  for (size_t i = 0; i < length; ++i) {
    oss << std::setw(2) << static_cast<int>(data[i]);
  }
  return oss.str();
}

[[noreturn]] void throw_git_error(const char *context) {
  const git_error *err = git_error_last();
  if (err && err->message) {
    throw std::runtime_error(std::string(context) + ": " + err->message);
  }
  throw std::runtime_error(context);
}

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

  const auto temp_dir = std::filesystem::temp_directory_path() /
                        std::filesystem::path("codex-git-probe-%%%%-%%%%");
  std::filesystem::create_directories(temp_dir);
  const auto temp_dir_str = temp_dir.string();

  git_repository *repo = nullptr;
  if (git_repository_init(&repo, temp_dir_str.c_str(), 1) != 0) {
    std::filesystem::remove_all(temp_dir);
    git_libgit2_shutdown();
    throw_git_error("libgit2 repository init failed");
  }

  static constexpr const char *kGitProbeUrl =
      "https://github.com/libgit2/libgit2.git";
  git_remote *remote = nullptr;
  if (git_remote_create_anonymous(&remote, repo, kGitProbeUrl) != 0) {
    git_repository_free(repo);
    std::filesystem::remove_all(temp_dir);
    git_libgit2_shutdown();
    throw_git_error("libgit2 remote creation failed");
  }

  std::cout << "[libgit2] Connecting to " << kGitProbeUrl
            << " for reference listing..." << std::endl;
  if (git_remote_connect(remote, GIT_DIRECTION_FETCH, nullptr, nullptr, nullptr) !=
      0) {
    git_remote_free(remote);
    git_repository_free(repo);
    std::filesystem::remove_all(temp_dir);
    git_libgit2_shutdown();
    throw_git_error("libgit2 remote connect failed");
  }

  const git_remote_head **refs = nullptr;
  size_t ref_count = 0;
  if (git_remote_ls(&refs, &ref_count, remote) != 0) {
    git_remote_disconnect(remote);
    git_remote_free(remote);
    git_repository_free(repo);
    std::filesystem::remove_all(temp_dir);
    git_libgit2_shutdown();
    throw_git_error("libgit2 remote reference enumeration failed");
  }

  if (ref_count == 0) {
    git_remote_disconnect(remote);
    git_remote_free(remote);
    git_repository_free(repo);
    std::filesystem::remove_all(temp_dir);
    git_libgit2_shutdown();
    throw std::runtime_error("libgit2 remote returned zero references");
  }

  std::cout << "[libgit2] Retrieved " << ref_count
            << " remote references; HEAD is " << refs[0]->name << std::endl;

  git_remote_disconnect(remote);
  git_remote_free(remote);
  git_repository_free(repo);
  std::error_code ignore_ec;
  std::filesystem::remove_all(temp_dir, ignore_ec);
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

  static constexpr const char *kCurlProbeUrl = "https://github.com/";
  std::cout << "[libcurl] Performing HTTPS HEAD request to " << kCurlProbeUrl
            << "..." << std::endl;
  curl_easy_setopt(curl, CURLOPT_URL, kCurlProbeUrl);
  curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "codex-cmake-test/1.0");
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

  const CURLcode perform_result = curl_easy_perform(curl);
  if (perform_result != CURLE_OK) {
    const char *err = curl_easy_strerror(perform_result);
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    throw std::runtime_error(std::string("libcurl HTTPS request failed: ") +
                             (err ? err : "unknown error"));
  }

  long response_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
  if (response_code < 200 || response_code >= 400) {
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    throw std::runtime_error("libcurl HTTPS probe returned non-success status: " +
                             std::to_string(response_code));
  }

  curl_easy_cleanup(curl);
  curl_global_cleanup();
  std::cout << "[libcurl] TLS backend: "
            << (info->ssl_version ? info->ssl_version : "unknown")
            << "; libssh2: "
            << (info->libssh_version ? info->libssh_version : "none")
            << "; response code " << response_code << std::endl;
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

void check_md5() {
  std::cout << "[md5] Hashing test vector via OpenSSL..." << std::endl;
  static constexpr std::string_view message =
      "The quick brown fox jumps over the lazy dog";
  static constexpr std::size_t kMd5DigestLength = 16;
  std::array<unsigned char, kMd5DigestLength> digest{};

  struct MdCtxDeleter {
    void operator()(EVP_MD_CTX *ctx) const {
      if (ctx) {
        EVP_MD_CTX_free(ctx);
      }
    }
  };

  std::unique_ptr<EVP_MD_CTX, MdCtxDeleter> ctx{EVP_MD_CTX_new()};
  if (!ctx) {
    throw std::runtime_error("OpenSSL failed to allocate MD context");
  }

  if (EVP_DigestInit_ex(ctx.get(), EVP_md5(), nullptr) != 1) {
    throw std::runtime_error("OpenSSL EVP_MD init failed");
  }
  if (EVP_DigestUpdate(ctx.get(), message.data(), message.size()) != 1) {
    throw std::runtime_error("OpenSSL EVP_MD update failed");
  }

  unsigned int written = 0;
  if (EVP_DigestFinal_ex(ctx.get(), digest.data(), &written) != 1) {
    throw std::runtime_error("OpenSSL EVP_MD finalization failed");
  }
  if (written != digest.size()) {
    throw std::runtime_error("OpenSSL MD5 produced unexpected length");
  }

  static constexpr std::array<unsigned char, kMd5DigestLength> expected_bytes{
      0x9e, 0x10, 0x7d, 0x9d, 0x37, 0x2b, 0xb6, 0x82,
      0x6b, 0xd8, 0x1d, 0x35, 0x42, 0xa4, 0x19, 0xd6};
  if (!std::equal(digest.begin(), digest.end(), expected_bytes.begin())) {
    throw std::runtime_error("OpenSSL MD5 produced unexpected digest: " +
                             to_hex(digest.data(), digest.size()));
  }

  std::cout << "[md5] Digest " << to_hex(digest.data(), digest.size())
            << " validated." << std::endl;
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
        check_md5();
    } catch (const std::exception &ex) {
        std::cerr << "Initialization failed: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "All dependencies initialized successfully." << std::endl;
    return EXIT_SUCCESS;
}

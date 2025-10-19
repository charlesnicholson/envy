#include "cmd_version.h"
#include "platform_windows.h"

#include "CLI11.hpp"
#include "archive.h"
#include "aws/core/Aws.h"
#include "aws/core/Version.h"
#include "aws/crt/Api.h"
#include "blake3.h"
#include "bzlib.h"
#include "curl/curl.h"
#include "git2.h"
#include "lzma.h"
#include "mbedtls/version.h"
#include "oneapi/tbb/version.h"
#include "tbb/flow_graph.h"
#include "zlib.h"
#include "zstd.h"

extern "C" {
#include "lua.h"
}

#include "libssh2.h"

#include <array>
#include <iostream>
#include <string>
#include <vector>

namespace envy {

cmd_version::cmd_version(cmd_version::cfg cfg) : cfg_{ std::move(cfg) } {}

void cmd_version::schedule(tbb::flow::graph &g) {
  node_.emplace(g, [](tbb::flow::continue_msg const &) {
    std::cout << "envy version 0.1.0\n\n";
    std::cout << "Third-party component versions:\n";

    int git_major{ 0 };
    int git_minor{ 0 };
    int git_revision{ 0 };
    git_libgit2_version(&git_major, &git_minor, &git_revision);
    std::cout << "  libgit2: " << git_major << '.' << git_minor << '.' << git_revision
              << '\n';

    curl_version_info_data const *curl_info{ curl_version_info(CURLVERSION_NOW) };
    std::cout << "  libcurl: " << curl_info->version;
    std::vector<std::string> curl_features;
    if (curl_info->features & CURL_VERSION_ZSTD) { curl_features.push_back("zstd"); }
    if (curl_info->features & CURL_VERSION_BROTLI) { curl_features.push_back("brotli"); }
    if (curl_info->features & CURL_VERSION_LIBZ) { curl_features.push_back("zlib"); }
    if (!curl_features.empty()) {
      std::cout << " (";
      for (size_t i{ 0 }; i < curl_features.size(); ++i) {
        if (i > 0) std::cout << ", ";
        std::cout << curl_features[i];
      }
      std::cout << ")";
    }
    std::cout << '\n';

    std::cout << "  libssh2: " << LIBSSH2_VERSION << '\n';

    std::array<char, 32> mbedtls_version{};
    mbedtls_version_get_string_full(mbedtls_version.data());
    std::cout << "  mbedTLS: " << mbedtls_version.data() << '\n';

    std::cout << "  libarchive: " << archive_version_details() << '\n';
    std::cout << "  Lua: " << LUA_RELEASE << '\n';
    std::cout << "  oneTBB: " << TBB_runtime_version() << '\n';
    std::cout << "  BLAKE3: " << BLAKE3_VERSION_STRING << '\n';
    std::cout << "  zlib: " << zlibVersion() << '\n';
    std::cout << "  bzip2: " << BZ2_bzlibVersion() << '\n';
    std::cout << "  zstd: " << ZSTD_versionString() << '\n';
    std::cout << "  liblzma: " << lzma_version_string() << '\n';
    std::cout << "  AWS SDK for C++: " << Aws::Version::GetVersionString() << '\n';

    Aws::Crt::ApiHandle crt_handle;
    auto const crt_version{ crt_handle.GetCrtVersion() };
    std::cout << "  AWS CRT: " << crt_version.major << '.' << crt_version.minor << '.'
              << crt_version.patch << '\n';

    std::cout << "  CLI11: " << CLI11_VERSION << '\n';
  });

  node_->try_put(tbb::flow::continue_msg{});
}

}  // namespace envy

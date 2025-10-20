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
#include "tui.h"
#include "zlib.h"
#include "zstd.h"

extern "C" {
#include "lua.h"
}

#include "libssh2.h"

#include <array>
#include <string>
#include <vector>

#ifndef ENVY_VERSION_STR
#error "ENVY_VERSION_STR must be defined by the build system"
#endif

namespace envy {

cmd_version::cmd_version(cmd_version::cfg cfg) : cfg_{ std::move(cfg) } {}

void cmd_version::schedule(tbb::flow::graph &g) {
  node_.emplace(g, [](tbb::flow::continue_msg const &) {
    tui::info("envy version %s\n\n", ENVY_VERSION_STR);
    tui::info("Third-party component versions:\n");

    int git_major{ 0 };
    int git_minor{ 0 };
    int git_revision{ 0 };
    git_libgit2_version(&git_major, &git_minor, &git_revision);
    tui::info("  libgit2: %d.%d.%d\n", git_major, git_minor, git_revision);

    curl_version_info_data const *curl_info{ curl_version_info(CURLVERSION_NOW) };
    std::vector<std::string> curl_features;
    if (curl_info->features & CURL_VERSION_ZSTD) { curl_features.push_back("zstd"); }
    if (curl_info->features & CURL_VERSION_BROTLI) { curl_features.push_back("brotli"); }
    if (curl_info->features & CURL_VERSION_LIBZ) { curl_features.push_back("zlib"); }
    if (!curl_features.empty()) {
      std::string features;
      for (size_t i{ 0 }; i < curl_features.size(); ++i) {
        if (i > 0) features.append(", ");
        features.append(curl_features[i]);
      }
      tui::info("  libcurl: %s (%s)\n", curl_info->version, features.c_str());
    } else {
      tui::info("  libcurl: %s\n", curl_info->version);
    }

    tui::info("  libssh2: %s\n", LIBSSH2_VERSION);

    std::array<char, 32> mbedtls_version{};
    mbedtls_version_get_string_full(mbedtls_version.data());
    tui::info("  mbedTLS: %s\n", mbedtls_version.data());

    tui::info("  libarchive: %s\n", archive_version_details());
    tui::info("  Lua: %s\n", LUA_RELEASE);
    tui::info("  oneTBB: %s\n", TBB_runtime_version());
    tui::info("  BLAKE3: %s\n", BLAKE3_VERSION_STRING);
    tui::info("  zlib: %s\n", zlibVersion());
    tui::info("  bzip2: %s\n", BZ2_bzlibVersion());
    tui::info("  zstd: %s\n", ZSTD_versionString());
    tui::info("  liblzma: %s\n", lzma_version_string());
    auto const *aws_sdk_version{ Aws::Version::GetVersionString() };
    tui::info("  AWS SDK for C++: %s\n", aws_sdk_version);

    Aws::Crt::ApiHandle crt_handle;
    auto const crt_version{ crt_handle.GetCrtVersion() };
    tui::info("  AWS CRT: %u.%u.%u\n",
              static_cast<unsigned>(crt_version.major),
              static_cast<unsigned>(crt_version.minor),
              static_cast<unsigned>(crt_version.patch));

    tui::info("  CLI11: %s\n", CLI11_VERSION);
  });

  node_->try_put(tbb::flow::continue_msg{});
}

}  // namespace envy

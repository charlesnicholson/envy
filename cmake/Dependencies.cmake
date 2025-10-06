include(FetchContent)
include(ExternalProject)

include("${CMAKE_CURRENT_LIST_DIR}/CodexFetchContent.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/DependencyPatches.cmake")

if(POLICY CMP0169)
    cmake_policy(PUSH)
    cmake_policy(SET CMP0169 OLD)
endif()

set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "Disable deprecated CMake warnings from third-party builds" FORCE)

set_property(GLOBAL PROPERTY JOB_POOLS codex_fetch=4)

# Persist fetched sources outside the build tree so deleting `out/build`
# forces a rebuild while reusing cached downloads.
cmake_path(APPEND PROJECT_SOURCE_DIR "out" "cache" "third_party" OUTPUT_VARIABLE CODEX_THIRDPARTY_CACHE_DIR)
file(MAKE_DIRECTORY "${CODEX_THIRDPARTY_CACHE_DIR}")
set(FETCHCONTENT_BASE_DIR "${CODEX_THIRDPARTY_CACHE_DIR}")

set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
if(DEFINED ENV{CODEX_FETCH_FULLY_DISCONNECTED})
    set(FETCHCONTENT_FULLY_DISCONNECTED ON)
endif()

# ---------------------------------------------------------------------------
# Third-party version catalog
# ---------------------------------------------------------------------------
set(CODEX_OPENSSL_VERSION "3.6.0")
set(CODEX_OPENSSL_ARCHIVE "openssl-${CODEX_OPENSSL_VERSION}.tar.gz")
set(CODEX_OPENSSL_URL "https://www.openssl.org/source/${CODEX_OPENSSL_ARCHIVE}")
set(CODEX_OPENSSL_SHA256 b6a5f44b7eb69e3fa35dbf15524405b44837a481d43d81daddde3ff21fcbb8e9)

set(CODEX_LIBSSH2_VERSION "1.11.1")
set(CODEX_LIBSSH2_ARCHIVE "libssh2-${CODEX_LIBSSH2_VERSION}.tar.gz")
set(CODEX_LIBSSH2_URL "https://www.libssh2.org/download/${CODEX_LIBSSH2_ARCHIVE}")
set(CODEX_LIBSSH2_SHA256 d9ec76cbe34db98eec3539fe2c899d26b0c837cb3eb466a56b0f109cabf658f7)

set(CODEX_LIBGIT2_VERSION "1.9.1")
set(CODEX_LIBGIT2_ARCHIVE "libgit2-${CODEX_LIBGIT2_VERSION}.tar.gz")
set(CODEX_LIBGIT2_URL "https://github.com/libgit2/libgit2/archive/refs/tags/v${CODEX_LIBGIT2_VERSION}.tar.gz")
set(CODEX_LIBGIT2_SHA256 14cab3014b2b7ad75970ff4548e83615f74d719afe00aa479b4a889c1e13fc00)

set(CODEX_LIBCURL_VERSION "8.16.0")
set(CODEX_LIBCURL_ARCHIVE "curl-${CODEX_LIBCURL_VERSION}.tar.xz")
set(CODEX_LIBCURL_URL "https://curl.se/download/${CODEX_LIBCURL_ARCHIVE}")
set(CODEX_LIBCURL_SHA256 40c8cddbcb6cc6251c03dea423a472a6cea4037be654ba5cf5dec6eb2d22ff1d)

set(CODEX_ONETBB_VERSION "2022.2.0")
set(CODEX_ONETBB_ARCHIVE "oneTBB-${CODEX_ONETBB_VERSION}.tar.gz")
set(CODEX_ONETBB_URL "https://github.com/uxlfoundation/oneTBB/archive/refs/tags/v${CODEX_ONETBB_VERSION}.tar.gz")
set(CODEX_ONETBB_SHA256 f0f78001c8c8edb4bddc3d4c5ee7428d56ae313254158ad1eec49eced57f6a5b)

set(CODEX_AWS_SDK_VERSION "1.11.661")
set(CODEX_AWS_SDK_ARCHIVE "aws-sdk-cpp-${CODEX_AWS_SDK_VERSION}.zip")
set(CODEX_AWS_SDK_URL "https://github.com/aws/aws-sdk-cpp/archive/refs/tags/${CODEX_AWS_SDK_VERSION}.zip")
set(CODEX_AWS_SDK_SHA256 504493b205a8a466751af8654b2f32e9917df9e75bcff5defdf72fe320837ba3)

set(CODEX_LIBARCHIVE_VERSION "3.8.1")
set(CODEX_LIBARCHIVE_ARCHIVE "libarchive-${CODEX_LIBARCHIVE_VERSION}.tar.gz")
set(CODEX_LIBARCHIVE_URL "https://www.libarchive.org/downloads/libarchive-${CODEX_LIBARCHIVE_VERSION}.tar.gz")
set(CODEX_LIBARCHIVE_SHA256 bde832a5e3344dc723cfe9cc37f8e54bde04565bfe6f136bc1bd31ab352e9fab)

set(CODEX_BLAKE3_VERSION "1.8.2")
set(CODEX_BLAKE3_ARCHIVE "blake3-${CODEX_BLAKE3_VERSION}.tar.gz")
set(CODEX_BLAKE3_URL "https://github.com/BLAKE3-team/BLAKE3/archive/refs/tags/${CODEX_BLAKE3_VERSION}.tar.gz")
set(CODEX_BLAKE3_SHA256 6b51aefe515969785da02e87befafc7fdc7a065cd3458cf1141f29267749e81f)

set(CODEX_LUA_VERSION "5.4.8")
set(CODEX_LUA_ARCHIVE "lua-${CODEX_LUA_VERSION}.tar.gz")
set(CODEX_LUA_URL "https://www.lua.org/ftp/lua-${CODEX_LUA_VERSION}.tar.gz")
set(CODEX_LUA_SHA256 4f18ddae154e793e46eeab727c59ef1c0c0c2b744e7b94219710d76f530629ae)

find_package(ZLIB REQUIRED)
find_library(RESOLV_LIBRARY resolv REQUIRED)

# Enforce static libraries across third-party builds.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build dependencies as static libraries" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "Disable dependency test targets" FORCE)

include("${CMAKE_CURRENT_LIST_DIR}/deps/OpenSSL.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/deps/Libssh2.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/deps/Libgit2.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/deps/Libcurl.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/deps/OneTBB.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/deps/AwsSdk.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/deps/Libarchive.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/deps/Blake3.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/deps/Lua.cmake")

# Aggregate -----------------------------------------------------------------
add_library(codex_thirdparty INTERFACE)
add_library(codex::thirdparty ALIAS codex_thirdparty)

target_link_libraries(codex_thirdparty
    INTERFACE
        codex::libgit2
        TBB::tbb
        libarchive::libarchive
        lua::lua
        blake3::blake3
        OpenSSL::SSL
        libssh2::libssh2
        AWS::aws-cpp-sdk-s3
        AWS::aws-cpp-sdk-sso
        AWS::aws-cpp-sdk-sso-oidc
        aws-crt-cpp
        ZLIB::ZLIB
        ${RESOLV_LIBRARY}
)

if(APPLE)
    target_link_libraries(codex_thirdparty INTERFACE
        "-framework SystemConfiguration"
        "-framework CoreFoundation"
        "-framework CoreServices"
        "-framework Security"
    )
endif()

target_compile_definitions(codex_thirdparty INTERFACE
    CURL_STATICLIB
)

target_include_directories(codex_thirdparty INTERFACE
    "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
    "$<BUILD_INTERFACE:${aws_sdk_SOURCE_DIR}/aws-cpp-sdk-core/include>"
    "$<BUILD_INTERFACE:${aws_sdk_SOURCE_DIR}/src/aws-cpp-sdk-core/include>"
    "$<BUILD_INTERFACE:${aws_sdk_BINARY_DIR}/generated/src/aws-cpp-sdk-core/include>"
    "$<BUILD_INTERFACE:${CODEX_AWSCRT_ROOT}/include>"
)

if(POLICY CMP0169)
    cmake_policy(POP)
endif()

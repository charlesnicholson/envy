include(FetchContent)
include(ExternalProject)

include("${CMAKE_CURRENT_LIST_DIR}/EnvyFetchContent.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/DependencyPatches.cmake")

if(POLICY CMP0169)
    cmake_policy(SET CMP0169 NEW)
endif()

set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "Disable deprecated CMake warnings from third-party builds" FORCE)

set_property(GLOBAL PROPERTY JOB_POOLS envy_fetch=4)

# Persist fetched sources outside the build tree so deleting `out/build`
# forces a rebuild while reusing cached downloads.
cmake_path(APPEND PROJECT_SOURCE_DIR "out" "cache" "third_party" OUTPUT_VARIABLE ENVY_THIRDPARTY_CACHE_DIR)
file(MAKE_DIRECTORY "${ENVY_THIRDPARTY_CACHE_DIR}")
set(FETCHCONTENT_BASE_DIR "${ENVY_THIRDPARTY_CACHE_DIR}")

set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
if(DEFINED ENV{ENVY_FETCH_FULLY_DISCONNECTED})
    set(FETCHCONTENT_FULLY_DISCONNECTED ON)
endif()

# ---------------------------------------------------------------------------
# Third-party version catalog
# ---------------------------------------------------------------------------
set(ENVY_MBEDTLS_VERSION "3.6.4")
set(ENVY_MBEDTLS_ARCHIVE "mbedtls-${ENVY_MBEDTLS_VERSION}-easy-make-lib.tar.xz")
set(ENVY_MBEDTLS_URL "https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-${ENVY_MBEDTLS_VERSION}/${ENVY_MBEDTLS_ARCHIVE}")
set(ENVY_MBEDTLS_SHA256 6a7ed66b4aca38836f0eff8d8fba72992bf0c7326337608ef01de469fd8368bd)

set(ENVY_LIBSSH2_VERSION "1.11.1")
set(ENVY_LIBSSH2_ARCHIVE "libssh2-${ENVY_LIBSSH2_VERSION}.tar.gz")
set(ENVY_LIBSSH2_URL "https://www.libssh2.org/download/${ENVY_LIBSSH2_ARCHIVE}")
set(ENVY_LIBSSH2_SHA256 d9ec76cbe34db98eec3539fe2c899d26b0c837cb3eb466a56b0f109cabf658f7)

set(ENVY_LIBGIT2_VERSION "1.9.1")
set(ENVY_LIBGIT2_ARCHIVE "libgit2-${ENVY_LIBGIT2_VERSION}.tar.gz")
set(ENVY_LIBGIT2_URL "https://github.com/libgit2/libgit2/archive/refs/tags/v${ENVY_LIBGIT2_VERSION}.tar.gz")
set(ENVY_LIBGIT2_SHA256 14cab3014b2b7ad75970ff4548e83615f74d719afe00aa479b4a889c1e13fc00)

set(ENVY_LIBCURL_VERSION "8.16.0")
set(ENVY_LIBCURL_ARCHIVE "curl-${ENVY_LIBCURL_VERSION}.tar.xz")
set(ENVY_LIBCURL_URL "https://curl.se/download/${ENVY_LIBCURL_ARCHIVE}")
set(ENVY_LIBCURL_SHA256 40c8cddbcb6cc6251c03dea423a472a6cea4037be654ba5cf5dec6eb2d22ff1d)

set(ENVY_ONETBB_VERSION "2022.2.0")
set(ENVY_ONETBB_ARCHIVE "oneTBB-${ENVY_ONETBB_VERSION}.tar.gz")
set(ENVY_ONETBB_URL "https://github.com/uxlfoundation/oneTBB/archive/refs/tags/v${ENVY_ONETBB_VERSION}.tar.gz")
set(ENVY_ONETBB_SHA256 f0f78001c8c8edb4bddc3d4c5ee7428d56ae313254158ad1eec49eced57f6a5b)

set(ENVY_AWS_SDK_VERSION "1.11.661")
set(ENVY_AWS_SDK_ARCHIVE "aws-sdk-cpp-${ENVY_AWS_SDK_VERSION}.zip")
set(ENVY_AWS_SDK_URL "https://github.com/aws/aws-sdk-cpp/archive/refs/tags/${ENVY_AWS_SDK_VERSION}.zip")
set(ENVY_AWS_SDK_SHA256 504493b205a8a466751af8654b2f32e9917df9e75bcff5defdf72fe320837ba3)

set(ENVY_LIBARCHIVE_VERSION "3.8.1")
set(ENVY_LIBARCHIVE_ARCHIVE "libarchive-${ENVY_LIBARCHIVE_VERSION}.tar.gz")
set(ENVY_LIBARCHIVE_URL "https://www.libarchive.org/downloads/libarchive-${ENVY_LIBARCHIVE_VERSION}.tar.gz")
set(ENVY_LIBARCHIVE_SHA256 bde832a5e3344dc723cfe9cc37f8e54bde04565bfe6f136bc1bd31ab352e9fab)

set(ENVY_BLAKE3_VERSION "1.8.2")
set(ENVY_BLAKE3_ARCHIVE "blake3-${ENVY_BLAKE3_VERSION}.tar.gz")
set(ENVY_BLAKE3_URL "https://github.com/BLAKE3-team/BLAKE3/archive/refs/tags/${ENVY_BLAKE3_VERSION}.tar.gz")
set(ENVY_BLAKE3_SHA256 6b51aefe515969785da02e87befafc7fdc7a065cd3458cf1141f29267749e81f)

set(ENVY_LUA_VERSION "5.4.8")
set(ENVY_LUA_ARCHIVE "lua-${ENVY_LUA_VERSION}.tar.gz")
set(ENVY_LUA_URL "https://www.lua.org/ftp/lua-${ENVY_LUA_VERSION}.tar.gz")
set(ENVY_LUA_SHA256 4f18ddae154e793e46eeab727c59ef1c0c0c2b744e7b94219710d76f530629ae)

set(ENVY_ZLIB_VERSION "1.3.1")
set(ENVY_ZLIB_ARCHIVE "zlib-${ENVY_ZLIB_VERSION}.tar.gz")
set(ENVY_ZLIB_URL "https://zlib.net/${ENVY_ZLIB_ARCHIVE}")
set(ENVY_ZLIB_SHA256 9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23)

set(PLATFORM_NETWORK_LIBS)
if(WIN32)
    set(PLATFORM_NETWORK_LIBS ws2_32 dnsapi iphlpapi advapi32 crypt32 wldap32 winhttp bcrypt)
else()
    find_library(RESOLV_LIBRARY resolv REQUIRED)
    set(PLATFORM_NETWORK_LIBS ${RESOLV_LIBRARY})
endif()
if(APPLE)
    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
    find_library(CORESERVICES_FRAMEWORK CoreServices REQUIRED)
    find_library(SYSTEMCONFIGURATION_FRAMEWORK SystemConfiguration REQUIRED)
    find_library(SECURITY_FRAMEWORK Security REQUIRED)
endif()

# Enforce static libraries across third-party builds.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build dependencies as static libraries" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "Disable dependency test targets" FORCE)

include("${CMAKE_CURRENT_LIST_DIR}/deps/Zlib.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/deps/MbedTLS.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/deps/Libssh2.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/deps/Libgit2.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/deps/Libcurl.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/deps/OneTBB.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/deps/AwsSdk.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/deps/Libarchive.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/deps/Blake3.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/deps/Lua.cmake")

# Aggregate -----------------------------------------------------------------
add_library(envy_thirdparty INTERFACE)
add_library(envy::thirdparty ALIAS envy_thirdparty)

target_link_libraries(envy_thirdparty
    INTERFACE
        envy::libgit2
        libssh2::libssh2
        MbedTLS::mbedtls
        MbedTLS::mbedx509
        MbedTLS::mbedcrypto
        ZLIB::ZLIB
        ${PLATFORM_NETWORK_LIBS}
        CURL::libcurl
        TBB::tbb
        libarchive::libarchive
        lua::lua
        blake3::blake3
        AWS::aws-cpp-sdk-s3
        AWS::aws-cpp-sdk-sso
        AWS::aws-cpp-sdk-sso-oidc
        aws-crt-cpp
)

if(APPLE)
    target_link_libraries(envy_thirdparty INTERFACE
        ${SYSTEMCONFIGURATION_FRAMEWORK}
        ${COREFOUNDATION_FRAMEWORK}
        ${CORESERVICES_FRAMEWORK}
        ${SECURITY_FRAMEWORK}
    )
endif()

target_compile_definitions(envy_thirdparty INTERFACE
    CURL_STATICLIB
)

target_include_directories(envy_thirdparty INTERFACE
    "$<BUILD_INTERFACE:${aws_sdk_SOURCE_DIR}/aws-cpp-sdk-core/include>"
    "$<BUILD_INTERFACE:${aws_sdk_SOURCE_DIR}/src/aws-cpp-sdk-core/include>"
    "$<BUILD_INTERFACE:${aws_sdk_BINARY_DIR}/generated/src/aws-cpp-sdk-core/include>"
    "$<BUILD_INTERFACE:${ENVY_AWSCRT_ROOT}/include>"
    $<$<BOOL:${ENVY_LIBCURL_INCLUDE}>:$<BUILD_INTERFACE:${ENVY_LIBCURL_INCLUDE}>>
    $<$<BOOL:${ENVY_LIBCURL_BINARY_INCLUDE}>:$<BUILD_INTERFACE:${ENVY_LIBCURL_BINARY_INCLUDE}>>
)

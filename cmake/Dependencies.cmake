include(FetchContent)
include(ExternalProject)

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

set(CODEX_LIBGIT2_REPOSITORY "https://github.com/libgit2/libgit2.git")
set(CODEX_LIBGIT2_TAG "v1.9.1")

set(CODEX_LIBCURL_VERSION "8.16.0")
set(CODEX_LIBCURL_ARCHIVE "curl-${CODEX_LIBCURL_VERSION}.tar.xz")
set(CODEX_LIBCURL_URL "https://curl.se/download/${CODEX_LIBCURL_ARCHIVE}")
set(CODEX_LIBCURL_SHA256 40c8cddbcb6cc6251c03dea423a472a6cea4037be654ba5cf5dec6eb2d22ff1d)

set(CODEX_ONETBB_REPOSITORY "https://github.com/oneapi-src/oneTBB.git")
set(CODEX_ONETBB_TAG "v2022.2.0")

set(CODEX_AWS_SDK_URL "https://github.com/aws/aws-sdk-cpp/archive/refs/tags/1.11.661.zip")
set(CODEX_AWS_SDK_SHA256 504493b205a8a466751af8654b2f32e9917df9e75bcff5defdf72fe320837ba3)

set(CODEX_LIBARCHIVE_REPOSITORY "https://github.com/libarchive/libarchive.git")
set(CODEX_LIBARCHIVE_TAG "v3.8.1")

set(CODEX_BLAKE3_REPOSITORY "https://github.com/BLAKE3-team/BLAKE3.git")
set(CODEX_BLAKE3_TAG "1.8.2")

set(CODEX_LUA_REPOSITORY "https://github.com/lua/lua.git")
set(CODEX_LUA_TAG "v5.4.8")

function(codex_fetchcontent_populate name human_name)
    string(TOLOWER "${name}" _codex_lower)
    cmake_path(APPEND FETCHCONTENT_BASE_DIR "${_codex_lower}-src" OUTPUT_VARIABLE _codex_source_dir)
    cmake_path(APPEND CMAKE_BINARY_DIR "_deps" "${_codex_lower}-build" OUTPUT_VARIABLE _codex_binary_dir)

    set(_codex_prev_defined FALSE)
    if(DEFINED FETCHCONTENT_FULLY_DISCONNECTED)
        set(_codex_prev_defined TRUE)
        set(_codex_prev_value "${FETCHCONTENT_FULLY_DISCONNECTED}")
    endif()

    if(EXISTS "${_codex_source_dir}")
        message(STATUS "[codex] Reusing cached ${human_name} sources at ${_codex_source_dir}")
        set(FETCHCONTENT_FULLY_DISCONNECTED ON)
    else()
        message(STATUS "[codex] No cached ${human_name} sources at ${_codex_source_dir}; fetching")
    endif()

    FetchContent_Populate(${name})
    FetchContent_GetProperties(${name})

    if(_codex_prev_defined)
        set(FETCHCONTENT_FULLY_DISCONNECTED "${_codex_prev_value}")
    else()
        unset(FETCHCONTENT_FULLY_DISCONNECTED)
    endif()

    set(${name}_SOURCE_DIR "${_codex_source_dir}" PARENT_SCOPE)
    set(${name}_BINARY_DIR "${_codex_binary_dir}" PARENT_SCOPE)
    if(DEFINED ${name}_POPULATED)
        set(${name}_POPULATED "${${name}_POPULATED}" PARENT_SCOPE)
    endif()

    unset(_codex_prev_defined)
    unset(_codex_prev_value)
    unset(_codex_source_dir)
    unset(_codex_binary_dir)
    unset(_codex_lower)
endfunction()

find_package(ZLIB REQUIRED)
find_library(RESOLV_LIBRARY resolv REQUIRED)

# Enforce static libraries across third-party builds.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build dependencies as static libraries" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "Disable dependency test targets" FORCE)

# OpenSSL --------------------------------------------------------------------
cmake_path(APPEND CMAKE_BINARY_DIR "_deps" "openssl-build" OUTPUT_VARIABLE OPENSSL_BINARY)
cmake_path(APPEND CODEX_THIRDPARTY_CACHE_DIR "${CODEX_OPENSSL_ARCHIVE}" OUTPUT_VARIABLE _openssl_archive)
set(_openssl_url "${CODEX_OPENSSL_URL}")
if(EXISTS "${_openssl_archive}")
    file(TO_CMAKE_PATH "${_openssl_archive}" _openssl_archive_norm)
    set(_openssl_url "file://${_openssl_archive_norm}")
endif()

find_program(PERL_EXECUTABLE perl REQUIRED)
find_program(OPENSSL_MAKE_COMMAND make REQUIRED)

FetchContent_Declare(openssl
    URL ${_openssl_url}
    URL_HASH SHA256=${CODEX_OPENSSL_SHA256}
)
FetchContent_GetProperties(openssl)
if(NOT openssl_POPULATED)
    message(STATUS "Fetching OpenSSL sources (openssl-${CODEX_OPENSSL_VERSION})...")
    codex_fetchcontent_populate(openssl "OpenSSL")
    FetchContent_GetProperties(openssl)

    set(_openssl_target "")
    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
            set(_openssl_target "darwin64-arm64-cc")
        else()
            set(_openssl_target "darwin64-x86_64-cc")
        endif()
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
            set(_openssl_target "linux-aarch64")
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
            set(_openssl_target "linux-x86_64")
        endif()
    endif()

    if(_openssl_target STREQUAL "")
        message(FATAL_ERROR "Unsupported platform for OpenSSL build: ${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR}")
    endif()

    message(STATUS "Configuring OpenSSL for ${_openssl_target}")
    set(_openssl_configure_stamp "${OPENSSL_BINARY}/.configured")
    if(NOT EXISTS "${_openssl_configure_stamp}")
        file(MAKE_DIRECTORY "${OPENSSL_BINARY}")
        execute_process(
            COMMAND ${PERL_EXECUTABLE} Configure ${_openssl_target} --prefix=${OPENSSL_BINARY} --libdir=lib no-shared no-tests no-apps no-dso
            WORKING_DIRECTORY "${openssl_SOURCE_DIR}"
            COMMAND_ERROR_IS_FATAL ANY
        )
        file(WRITE "${_openssl_configure_stamp}" "configured\n")
    endif()
    unset(_openssl_configure_stamp)

    add_custom_command(
        OUTPUT "${OPENSSL_BINARY}/lib/libssl.a" "${OPENSSL_BINARY}/lib/libcrypto.a"
        COMMAND ${OPENSSL_MAKE_COMMAND} -j
        COMMAND ${OPENSSL_MAKE_COMMAND} install_sw
        WORKING_DIRECTORY "${openssl_SOURCE_DIR}"
        COMMENT "Building OpenSSL"
        VERBATIM
    )

    add_custom_target(openssl ALL
        DEPENDS "${OPENSSL_BINARY}/lib/libssl.a" "${OPENSSL_BINARY}/lib/libcrypto.a")
endif()

unset(_openssl_archive)
unset(_openssl_archive_norm)
unset(_openssl_url)

set(_OPENSSL_INCLUDE_DIR "${OPENSSL_BINARY}/include")
set(_OPENSSL_SSL_LIBRARY "${OPENSSL_BINARY}/lib/libssl.a")
set(_OPENSSL_CRYPTO_LIBRARY "${OPENSSL_BINARY}/lib/libcrypto.a")

file(MAKE_DIRECTORY "${_OPENSSL_INCLUDE_DIR}" "${OPENSSL_BINARY}/lib")

if(NOT TARGET OpenSSL::Crypto)
    add_library(openssl_crypto STATIC IMPORTED GLOBAL)
    set_target_properties(openssl_crypto PROPERTIES
        IMPORTED_LOCATION "${_OPENSSL_CRYPTO_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${_OPENSSL_INCLUDE_DIR}")
    add_dependencies(openssl_crypto openssl)
    add_library(OpenSSL::Crypto ALIAS openssl_crypto)
endif()

if(NOT TARGET OpenSSL::SSL)
    add_library(openssl_ssl STATIC IMPORTED GLOBAL)
    set_target_properties(openssl_ssl PROPERTIES
        IMPORTED_LOCATION "${_OPENSSL_SSL_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${_OPENSSL_INCLUDE_DIR}"
        INTERFACE_LINK_LIBRARIES OpenSSL::Crypto)
    add_dependencies(openssl_ssl openssl)
    add_library(OpenSSL::SSL ALIAS openssl_ssl)
endif()

set(OPENSSL_INCLUDE_DIR "${_OPENSSL_INCLUDE_DIR}" CACHE PATH "" FORCE)
set(OPENSSL_CRYPTO_LIBRARY "${_OPENSSL_CRYPTO_LIBRARY}" CACHE FILEPATH "" FORCE)
set(OPENSSL_SSL_LIBRARY "${_OPENSSL_SSL_LIBRARY}" CACHE FILEPATH "" FORCE)

# libssh2 -------------------------------------------------------------------
set(LIBSSH2_WITH_MBEDTLS OFF CACHE BOOL "" FORCE)
set(LIBSSH2_WITH_OPENSSL ON CACHE BOOL "" FORCE)
set(LIBSSH2_WITH_LIBGCRYPT OFF CACHE BOOL "" FORCE)
set(LIBSSH2_WITH_WINCNG OFF CACHE BOOL "" FORCE)
set(CRYPTO_BACKEND OpenSSL CACHE STRING "" FORCE)
set(ENABLE_ZLIB_COMPRESSION ON CACHE BOOL "" FORCE)
set(LIBSSH2_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
cmake_path(APPEND CODEX_THIRDPARTY_CACHE_DIR "${CODEX_LIBSSH2_ARCHIVE}" OUTPUT_VARIABLE _libssh2_archive)
set(_libssh2_url "${CODEX_LIBSSH2_URL}")
if(EXISTS "${_libssh2_archive}")
    file(TO_CMAKE_PATH "${_libssh2_archive}" _libssh2_archive_norm)
    set(_libssh2_url "file://${_libssh2_archive_norm}")
endif()

FetchContent_Declare(libssh2
    URL ${_libssh2_url}
    URL_HASH SHA256=${CODEX_LIBSSH2_SHA256}
)
FetchContent_GetProperties(libssh2)
if(NOT libssh2_POPULATED)
    codex_fetchcontent_populate(libssh2 "libssh2")
    FetchContent_GetProperties(libssh2)
    set(_libssh2_cmake "${libssh2_SOURCE_DIR}/CMakeLists.txt")
    if(EXISTS "${_libssh2_cmake}")
        file(READ "${_libssh2_cmake}" _libssh2_contents)
        set(_libssh2_patched "${_libssh2_contents}")
        string(REPLACE "cmake_minimum_required(VERSION 3.1)" "cmake_minimum_required(VERSION 3.5)" _libssh2_patched "${_libssh2_patched}")
        string(REPLACE "project(libssh2 C)\n\nset(CMAKE_MODULE_PATH" "project(libssh2 C)\nset(CMAKE_SOURCE_DIR \"\${PROJECT_SOURCE_DIR}\")\n\nset(CMAKE_MODULE_PATH" _libssh2_patched "${_libssh2_patched}")
        if(NOT _libssh2_patched STREQUAL _libssh2_contents)
            file(WRITE "${_libssh2_cmake}" "${_libssh2_patched}")
        endif()
    endif()
    set(_libssh2_original_cmake_source_dir "${CMAKE_SOURCE_DIR}")
    set(CMAKE_SOURCE_DIR "${libssh2_SOURCE_DIR}")
    set(_libssh2_pc_source "${libssh2_SOURCE_DIR}/libssh2.pc.in")
    set(_libssh2_pc_dest "${PROJECT_SOURCE_DIR}/libssh2.pc.in")
    set(_libssh2_pc_copied FALSE)
    if(NOT EXISTS "${_libssh2_pc_dest}")
        file(COPY "${_libssh2_pc_source}" DESTINATION "${PROJECT_SOURCE_DIR}")
        set(_libssh2_pc_copied TRUE)
    endif()
    add_subdirectory(${libssh2_SOURCE_DIR} ${libssh2_BINARY_DIR})
    if(_libssh2_pc_copied)
        file(REMOVE "${_libssh2_pc_dest}")
    endif()
    unset(_libssh2_pc_source)
    unset(_libssh2_pc_dest)
    unset(_libssh2_pc_copied)
    set(CMAKE_SOURCE_DIR "${_libssh2_original_cmake_source_dir}")
    unset(_libssh2_original_cmake_source_dir)
endif()
unset(_libssh2_archive)
unset(_libssh2_archive_norm)
unset(_libssh2_url)
if(TARGET libssh2::libssh2)
    set(_libssh2_primary_target libssh2::libssh2)
elseif(TARGET libssh2_static)
    set(_libssh2_primary_target libssh2_static)
elseif(TARGET libssh2)
    set(_libssh2_primary_target libssh2)
else()
    message(FATAL_ERROR "libssh2 target was not created by FetchContent")
endif()

if(NOT TARGET libssh2::libssh2)
    add_library(libssh2::libssh2 ALIAS ${_libssh2_primary_target})
    add_dependencies(${_libssh2_primary_target} openssl)
    set_target_properties(${_libssh2_primary_target} PROPERTIES INTERFACE_LINK_LIBRARIES "")
endif()

set(Libssh2_DIR "${libssh2_BINARY_DIR}" CACHE PATH "" FORCE)
set(LIBSSH2_DIR "${libssh2_BINARY_DIR}" CACHE PATH "" FORCE)
set(LIBSSH2_LIBRARY "${libssh2_BINARY_DIR}/src/libssh2.a" CACHE FILEPATH "" FORCE)
set(LIBSSH2_LIBRARIES "${LIBSSH2_LIBRARY}" CACHE STRING "" FORCE)
set(LIBSSH2_LIBRARY_DIR "${libssh2_BINARY_DIR}/src" CACHE PATH "" FORCE)
set(LIBSSH2_INCLUDE_DIR "${libssh2_SOURCE_DIR}/include" CACHE PATH "" FORCE)
set(LIBSSH2_INCLUDE_DIRS "${libssh2_SOURCE_DIR}/include" CACHE PATH "" FORCE)

set(_codex_libssh2_actual "${_libssh2_primary_target}")
if(_codex_libssh2_actual STREQUAL "libssh2::libssh2")
    get_target_property(_codex_libssh2_actual libssh2::libssh2 ALIASED_TARGET)
endif()
if(_codex_libssh2_actual AND TARGET ${_codex_libssh2_actual})
    target_compile_definitions(${_codex_libssh2_actual} PRIVATE __STDC_WANT_LIB_EXT1__=1)
endif()
unset(_codex_libssh2_actual)

# Ensure pkg-config lookups consider only system defaults; the build patches
# libgit2 to rely on the in-tree libssh2 target instead.
set(ENV{PKG_CONFIG_PATH} "")

# libgit2 -------------------------------------------------------------------
set(USE_HTTPS SecureTransport CACHE STRING "" FORCE)
set(USE_SSH ON CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_CLI OFF CACHE BOOL "" FORCE)
set(CMAKE_DISABLE_FIND_PACKAGE_PkgConfig ON)
FetchContent_Declare(libgit2
    GIT_REPOSITORY ${CODEX_LIBGIT2_REPOSITORY}
    GIT_TAG ${CODEX_LIBGIT2_TAG}
    GIT_SHALLOW TRUE
)
FetchContent_GetProperties(libgit2)
if(NOT libgit2_POPULATED)
    codex_fetchcontent_populate(libgit2 "libgit2")
    FetchContent_GetProperties(libgit2)
endif()

set(_codex_libgit2_select "${libgit2_SOURCE_DIR}/cmake/SelectSSH.cmake")
if(EXISTS "${_codex_libgit2_select}")
    set(_codex_libgit2_patch_script "${CMAKE_BINARY_DIR}/codex_patch_libgit2_select.py")
    set(_codex_libgit2_patch_template [==[
import pathlib, re
path = pathlib.Path(r"@SELECT_PATH@")
text = path.read_text()
if 'codex override v2' not in text:
    text = text.replace('find_pkglibraries(LIBSSH2 libssh2)\n', 'if(TARGET libssh2::libssh2) # codex override v2\n\tset(LIBSSH2_FOUND 1)\n\tset(LIBSSH2_INCLUDE_DIRS "@LIBSSH2_SOURCE@/include")\n\tset(LIBSSH2_LIBRARY_DIRS "@LIBSSH2_BINARY@/src")\n\tset(LIBSSH2_LIBRARIES "@LIBSSH2_BINARY@/src/libssh2.a")\n\tset(LIBSSH2_LDFLAGS "")\nelse()\n\tfind_pkglibraries(LIBSSH2 libssh2)\nendif()\n')
    text = re.sub(r'\tcheck_library_exists\("\${LIBSSH2_LIBRARIES}" libssh2_userauth_publickey_frommemory "\${LIBSSH2_LIBRARY_DIRS}" HAVE_LIBSSH2_MEMORY_CREDENTIALS\)\n\tif\(HAVE_LIBSSH2_MEMORY_CREDENTIALS\)\n\t\tset\(GIT_SSH_MEMORY_CREDENTIALS 1\)\n\tendif\(\)\n', 'if(NOT TARGET libssh2::libssh2) # codex override v2\n\tcheck_library_exists("@LIBSSH2_BINARY@/src/libssh2.a" libssh2_userauth_publickey_frommemory "@LIBSSH2_BINARY@/src" HAVE_LIBSSH2_MEMORY_CREDENTIALS)\n\tif(HAVE_LIBSSH2_MEMORY_CREDENTIALS)\n\t\tset(GIT_SSH_MEMORY_CREDENTIALS 1)\n\tendif()\nelse()\n\tset(GIT_SSH_MEMORY_CREDENTIALS 1)\nendif()\n', text, 1)
    path.write_text(text)
]==])
    set(SELECT_PATH "${_codex_libgit2_select}")
    set(LIBSSH2_SOURCE "${libssh2_SOURCE_DIR}")
    set(LIBSSH2_BINARY "${libssh2_BINARY_DIR}")
    string(CONFIGURE "${_codex_libgit2_patch_template}" _codex_libgit2_patch_content @ONLY)
    file(WRITE "${_codex_libgit2_patch_script}" "${_codex_libgit2_patch_content}")
    execute_process(COMMAND python3 "${_codex_libgit2_patch_script}"
        COMMAND_ERROR_IS_FATAL ANY)
    file(REMOVE "${_codex_libgit2_patch_script}")
    unset(_codex_libgit2_patch_template)
    unset(_codex_libgit2_patch_content)
    unset(SELECT_PATH)
    unset(LIBSSH2_SOURCE)
    unset(LIBSSH2_BINARY)
endif()
unset(_codex_libgit2_select)
add_subdirectory(${libgit2_SOURCE_DIR} ${libgit2_BINARY_DIR})
if(TARGET libgit2package)
    add_library(codex::libgit2 ALIAS libgit2package)
    target_include_directories(libgit2package INTERFACE
        "$<BUILD_INTERFACE:${libgit2_SOURCE_DIR}/include>"
        "$<BUILD_INTERFACE:${libgit2_BINARY_DIR}/include>")
elseif(TARGET git2)
    add_library(codex::libgit2 ALIAS git2)
elseif(TARGET libgit2)
    add_library(codex::libgit2 ALIAS libgit2)
else()
    message(FATAL_ERROR "libgit2 target was not created by FetchContent")
endif()

foreach(_codex_git_target IN ITEMS libgit2package git2 libgit2)
    if(TARGET ${_codex_git_target})
        get_target_property(_codex_git_iface ${_codex_git_target} INTERFACE_LINK_LIBRARIES)
        if(_codex_git_iface)
            list(REMOVE_ITEM _codex_git_iface libssh2::libssh2 "${LIBSSH2_LIBRARY}")
            set_target_properties(${_codex_git_target} PROPERTIES
                INTERFACE_LINK_LIBRARIES "${_codex_git_iface}")
        endif()
    endif()
endforeach()
unset(_codex_git_target)
unset(_codex_git_iface)

set(_codex_libgit2_warning_silencers
    $<$<COMPILE_LANG_AND_ID:C,AppleClang>:-Wno-declaration-after-statement>
    $<$<COMPILE_LANG_AND_ID:C,AppleClang>:-Wno-unused-but-set-parameter>
    $<$<COMPILE_LANG_AND_ID:C,AppleClang>:-Wno-single-bit-bitfield-constant-conversion>
    $<$<COMPILE_LANG_AND_ID:C,AppleClang>:-Wno-array-parameter>
)
foreach(_libgit2_target IN ITEMS libgit2 libgit2package util ntlmclient http-parser xdiff)
    if(TARGET ${_libgit2_target})
        target_compile_options(${_libgit2_target} PRIVATE ${_codex_libgit2_warning_silencers})
    endif()
endforeach()

unset(_libgit2_target)
unset(_codex_libgit2_warning_silencers)
unset(CMAKE_DISABLE_FIND_PACKAGE_PkgConfig)

# libcurl -------------------------------------------------------------------
set(CMAKE_USE_OPENSSL ON CACHE BOOL "" FORCE)
set(CURL_USE_OPENSSL ON CACHE BOOL "" FORCE)
set(CURL_USE_SECTRANSP OFF CACHE BOOL "" FORCE)
set(CURL_ZLIB ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_LDAP ON CACHE BOOL "" FORCE)
set(ENABLE_THREADED_RESOLVER ON CACHE BOOL "" FORCE)
set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
set(CURL_ENABLE_SSL ON CACHE BOOL "" FORCE)
set(CURL_BROTLI OFF CACHE STRING "" FORCE)
set(CURL_ZSTD OFF CACHE STRING "" FORCE)
set(USE_NGHTTP2 OFF CACHE BOOL "" FORCE)
set(USE_LIBIDN2 OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)

set(_curl_ca_bundle "")
if(APPLE)
    set(_curl_ca_bundle "/etc/ssl/cert.pem")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_curl_ca_bundle "/etc/ssl/certs/ca-certificates.crt")
endif()

if(_curl_ca_bundle AND EXISTS "${_curl_ca_bundle}")
    set(CURL_CA_BUNDLE "${_curl_ca_bundle}" CACHE STRING "" FORCE)
else()
    set(CURL_CA_BUNDLE "auto" CACHE STRING "" FORCE)
endif()
unset(_curl_ca_bundle)
set(CMAKE_DISABLE_FIND_PACKAGE_PkgConfig ON)
cmake_path(APPEND CODEX_THIRDPARTY_CACHE_DIR "${CODEX_LIBCURL_ARCHIVE}" OUTPUT_VARIABLE _curl_archive)
set(_curl_url "${CODEX_LIBCURL_URL}")
if(EXISTS "${_curl_archive}")
    file(TO_CMAKE_PATH "${_curl_archive}" _curl_archive_norm)
    set(_curl_url "file://${_curl_archive_norm}")
endif()

FetchContent_Declare(libcurl
    URL ${_curl_url}
    URL_HASH SHA256=${CODEX_LIBCURL_SHA256}
)
FetchContent_GetProperties(libcurl)
if(NOT libcurl_POPULATED)
    codex_fetchcontent_populate(libcurl "libcurl")
    FetchContent_GetProperties(libcurl)
endif()
add_subdirectory(${libcurl_SOURCE_DIR} ${libcurl_BINARY_DIR})
unset(_curl_archive)
unset(_curl_archive_norm)
unset(_curl_url)
if(NOT TARGET CURL::libcurl)
    add_library(CURL::libcurl ALIAS libcurl)
endif()
if(TARGET libcurl_static)
    add_dependencies(libcurl_static openssl)
    if(DEFINED _libssh2_primary_target)
        add_dependencies(libcurl_static ${_libssh2_primary_target})
    endif()
    set_target_properties(libcurl_static PROPERTIES INTERFACE_LINK_LIBRARIES "")
endif()
if(TARGET libcurl_shared)
    add_dependencies(libcurl_shared openssl)
    if(DEFINED _libssh2_primary_target)
        add_dependencies(libcurl_shared ${_libssh2_primary_target})
    endif()
    set_target_properties(libcurl_shared PROPERTIES INTERFACE_LINK_LIBRARIES "")
endif()
set(CURL_INCLUDE_DIR "${libcurl_SOURCE_DIR}/include" CACHE PATH "" FORCE)
set(CURL_LIBRARY "${libcurl_BINARY_DIR}/lib/libcurl.a" CACHE FILEPATH "" FORCE)
set(CURL_LIBRARIES "${CURL_LIBRARY}" CACHE STRING "" FORCE)
unset(_libssh2_primary_target)
unset(CMAKE_DISABLE_FIND_PACKAGE_PkgConfig)

# oneTBB --------------------------------------------------------------------
set(TBB_TEST OFF CACHE BOOL "" FORCE)
set(TBB_STRICT OFF CACHE BOOL "" FORCE)
FetchContent_Declare(oneTBB
    GIT_REPOSITORY ${CODEX_ONETBB_REPOSITORY}
    GIT_TAG ${CODEX_ONETBB_TAG}
    GIT_SHALLOW TRUE
)
FetchContent_GetProperties(oneTBB)
if(NOT oneTBB_POPULATED)
    codex_fetchcontent_populate(oneTBB "oneTBB")
    FetchContent_GetProperties(oneTBB)
endif()
add_subdirectory(${oneTBB_SOURCE_DIR} ${oneTBB_BINARY_DIR})

# AWS SDK -------------------------------------------------------------------
set(AWS_SDK_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(AWS_SDK_CPP_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(AWS_SDK_CPP_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(AWS_SDK_CPP_CUSTOM_MEMORY_MANAGEMENT OFF CACHE BOOL "" FORCE)
set(BUILD_ONLY "s3;sso;sso-oidc" CACHE STRING "" FORCE)
set(AWS_BUILD_ONLY "s3;sso;sso-oidc" CACHE STRING "" FORCE)
set(AWS_SDK_CPP_BUILD_ONLY "s3;sso;sso-oidc" CACHE STRING "" FORCE)
set(ENFORCE_SUBMODULE_VERSIONS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(aws_sdk
    URL ${CODEX_AWS_SDK_URL}
    URL_HASH SHA256=${CODEX_AWS_SDK_SHA256}
)
FetchContent_GetProperties(aws_sdk)
if(NOT aws_sdk_POPULATED OR NOT EXISTS "${aws_sdk_SOURCE_DIR}/CMakeLists.txt")
    codex_fetchcontent_populate(aws_sdk "AWS SDK for C++")
    FetchContent_GetProperties(aws_sdk)
endif()

set(_aws_crt_root "${aws_sdk_SOURCE_DIR}/crt/aws-crt-cpp")
set(_aws_crt_marker "${_aws_crt_root}/crt/aws-c-common/CMakeLists.txt")
if(NOT EXISTS "${_aws_crt_root}/CMakeLists.txt" OR NOT EXISTS "${_aws_crt_marker}")
    message(STATUS "[codex] Prefetching AWS CRT dependencies...")
    set(_codex_prefetch_path "$ENV{PATH}")
    if(_codex_prefetch_path)
        set(_codex_prefetch_path "${PROJECT_SOURCE_DIR}/tools:${_codex_prefetch_path}")
    else()
        set(_codex_prefetch_path "${PROJECT_SOURCE_DIR}/tools")
    endif()
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E env PATH=${_codex_prefetch_path} bash "${aws_sdk_SOURCE_DIR}/prefetch_crt_dependency.sh"
        WORKING_DIRECTORY "${aws_sdk_SOURCE_DIR}"
        COMMAND_ERROR_IS_FATAL ANY)
    unset(_codex_prefetch_path)
    file(REMOVE_RECURSE "${_aws_crt_root}/crt/tmp")
endif()

set(_codex_prev_build_testing ${BUILD_TESTING})
set(BUILD_TESTING OFF CACHE BOOL "Disable tests inside AWS SDK build" FORCE)
add_subdirectory(${aws_sdk_SOURCE_DIR} ${aws_sdk_BINARY_DIR})
set(BUILD_TESTING ${_codex_prev_build_testing} CACHE BOOL "Restart project testing flag" FORCE)
unset(_codex_prev_build_testing)

# libarchive ----------------------------------------------------------------
set(ENABLE_CAT OFF CACHE BOOL "" FORCE)
set(ENABLE_TAR OFF CACHE BOOL "" FORCE)
set(ENABLE_CPIO OFF CACHE BOOL "" FORCE)
set(ENABLE_XATTR OFF CACHE BOOL "" FORCE)
set(ENABLE_ACL OFF CACHE BOOL "" FORCE)
set(ENABLE_TEST OFF CACHE BOOL "" FORCE)
set(ENABLE_LIBB2 OFF CACHE BOOL "" FORCE)
set(ENABLE_LZ4 OFF CACHE BOOL "" FORCE)
set(ENABLE_LZMA OFF CACHE BOOL "" FORCE)
set(ENABLE_ZLIB OFF CACHE BOOL "" FORCE)
set(ENABLE_BZip2 OFF CACHE BOOL "" FORCE)
set(ENABLE_ZSTD OFF CACHE BOOL "" FORCE)
set(ENABLE_LZO OFF CACHE BOOL "" FORCE)
set(ENABLE_PCREPOSIX OFF CACHE BOOL "" FORCE)
set(ENABLE_LIBXML2 OFF CACHE BOOL "" FORCE)
set(ENABLE_ICONV OFF CACHE BOOL "" FORCE)
set(ENABLE_OPENSSL ON CACHE BOOL "" FORCE)
set(ENABLE_EXPAT OFF CACHE BOOL "" FORCE)
set(ENABLE_CNG OFF CACHE BOOL "" FORCE)
set(ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(LIBARCHIVE_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(ENABLE_COMMONCRYPTO OFF CACHE BOOL "" FORCE)
FetchContent_Declare(libarchive
    GIT_REPOSITORY ${CODEX_LIBARCHIVE_REPOSITORY}
    GIT_TAG ${CODEX_LIBARCHIVE_TAG}
    GIT_SHALLOW TRUE
)
FetchContent_GetProperties(libarchive)
if(NOT libarchive_POPULATED)
    codex_fetchcontent_populate(libarchive "libarchive")
    FetchContent_GetProperties(libarchive)
    set(_libarchive_cmake "${libarchive_SOURCE_DIR}/CMakeLists.txt")
    if(EXISTS "${_libarchive_cmake}")
        file(READ "${_libarchive_cmake}" _libarchive_contents)
        set(_libarchive_patched "${_libarchive_contents}")
        string(REPLACE "CMAKE_MINIMUM_REQUIRED(VERSION 2.8.12 FATAL_ERROR)" "cmake_minimum_required(VERSION 3.5)" _libarchive_patched "${_libarchive_patched}")
        string(REPLACE "cmake_minimum_required(VERSION 2.8.12 FATAL_ERROR)" "cmake_minimum_required(VERSION 3.5)" _libarchive_patched "${_libarchive_patched}")
        if(NOT _libarchive_patched STREQUAL _libarchive_contents)
            file(WRITE "${_libarchive_cmake}" "${_libarchive_patched}")
        endif()
    endif()
    add_subdirectory(${libarchive_SOURCE_DIR} ${libarchive_BINARY_DIR})
endif()
if(TARGET archive)
    add_library(libarchive::libarchive ALIAS archive)
    target_include_directories(archive INTERFACE
        ${libarchive_SOURCE_DIR}/libarchive
        ${libarchive_BINARY_DIR}/libarchive)
elseif(TARGET libarchive)
    add_library(libarchive::libarchive ALIAS libarchive)
    target_include_directories(libarchive INTERFACE
        ${libarchive_SOURCE_DIR}/libarchive
        ${libarchive_BINARY_DIR}/libarchive)
elseif(TARGET archive_static)
    add_library(libarchive::libarchive ALIAS archive_static)
    target_include_directories(archive_static INTERFACE
        ${libarchive_SOURCE_DIR}/libarchive
        ${libarchive_BINARY_DIR}/libarchive)
else()
    message(FATAL_ERROR "libarchive target was not created by FetchContent")
endif()

# BLAKE3 --------------------------------------------------------------------
FetchContent_Declare(blake3
    GIT_REPOSITORY ${CODEX_BLAKE3_REPOSITORY}
    GIT_TAG ${CODEX_BLAKE3_TAG}
    GIT_SHALLOW TRUE
)
FetchContent_GetProperties(blake3)
if(NOT blake3_POPULATED)
    codex_fetchcontent_populate(blake3 "BLAKE3")
    FetchContent_GetProperties(blake3)
    set(BLAKE3_SOURCES
        "${blake3_SOURCE_DIR}/c/blake3.c"
        "${blake3_SOURCE_DIR}/c/blake3_dispatch.c"
        "${blake3_SOURCE_DIR}/c/blake3_portable.c"
    )
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
        list(APPEND BLAKE3_SOURCES
            "${blake3_SOURCE_DIR}/c/blake3_sse2_x86-64_unix.S"
            "${blake3_SOURCE_DIR}/c/blake3_sse41_x86-64_unix.S"
            "${blake3_SOURCE_DIR}/c/blake3_avx2_x86-64_unix.S"
            "${blake3_SOURCE_DIR}/c/blake3_avx512_x86-64_unix.S"
        )
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "(aarch64|arm64)")
        list(APPEND BLAKE3_SOURCES "${blake3_SOURCE_DIR}/c/blake3_neon.c")
    endif()
    add_library(blake3 STATIC ${BLAKE3_SOURCES})
    target_include_directories(blake3 PUBLIC "${blake3_SOURCE_DIR}/c")
    target_compile_features(blake3 PUBLIC c_std_99)
    add_library(blake3::blake3 ALIAS blake3)
endif()

# Lua -----------------------------------------------------------------------
FetchContent_Declare(lua
    GIT_REPOSITORY ${CODEX_LUA_REPOSITORY}
    GIT_TAG ${CODEX_LUA_TAG}
    GIT_SHALLOW TRUE
)
FetchContent_GetProperties(lua)
if(NOT lua_POPULATED)
    codex_fetchcontent_populate(lua "Lua")
    FetchContent_GetProperties(lua)
    file(GLOB LUA_CORE_SOURCES
        "${lua_SOURCE_DIR}/lapi.c"
        "${lua_SOURCE_DIR}/lcode.c"
        "${lua_SOURCE_DIR}/lctype.c"
        "${lua_SOURCE_DIR}/ldebug.c"
        "${lua_SOURCE_DIR}/ldo.c"
        "${lua_SOURCE_DIR}/ldump.c"
        "${lua_SOURCE_DIR}/lfunc.c"
        "${lua_SOURCE_DIR}/lgc.c"
        "${lua_SOURCE_DIR}/llex.c"
        "${lua_SOURCE_DIR}/lmem.c"
        "${lua_SOURCE_DIR}/lobject.c"
        "${lua_SOURCE_DIR}/lopcodes.c"
        "${lua_SOURCE_DIR}/lparser.c"
        "${lua_SOURCE_DIR}/lstate.c"
        "${lua_SOURCE_DIR}/lstring.c"
        "${lua_SOURCE_DIR}/ltable.c"
        "${lua_SOURCE_DIR}/ltm.c"
        "${lua_SOURCE_DIR}/lundump.c"
        "${lua_SOURCE_DIR}/lvm.c"
        "${lua_SOURCE_DIR}/lzio.c"
        "${lua_SOURCE_DIR}/lauxlib.c"
        "${lua_SOURCE_DIR}/lbaselib.c"
        "${lua_SOURCE_DIR}/lcorolib.c"
        "${lua_SOURCE_DIR}/ldblib.c"
        "${lua_SOURCE_DIR}/liolib.c"
        "${lua_SOURCE_DIR}/lmathlib.c"
        "${lua_SOURCE_DIR}/loslib.c"
        "${lua_SOURCE_DIR}/lstrlib.c"
        "${lua_SOURCE_DIR}/ltablib.c"
        "${lua_SOURCE_DIR}/lutf8lib.c"
        "${lua_SOURCE_DIR}/loadlib.c"
        "${lua_SOURCE_DIR}/linit.c"
    )
    add_library(lua STATIC ${LUA_CORE_SOURCES})
    target_include_directories(lua PUBLIC "${lua_SOURCE_DIR}")
    target_compile_definitions(lua PUBLIC LUA_COMPAT_5_3)
    set_target_properties(lua PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED ON)
    target_compile_options(lua PRIVATE $<$<COMPILE_LANGUAGE:C>:-std=c99>)
    add_library(lua::lua ALIAS lua)
endif()

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

add_dependencies(codex_thirdparty openssl)
add_dependencies(codex_thirdparty aws-cpp-sdk-s3)
add_dependencies(codex_thirdparty aws-cpp-sdk-sso)
add_dependencies(codex_thirdparty aws-cpp-sdk-sso-oidc)
add_dependencies(codex_thirdparty aws-crt-cpp)

target_compile_definitions(codex_thirdparty INTERFACE
    CURL_STATICLIB
)

target_include_directories(codex_thirdparty INTERFACE
    "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
    "$<BUILD_INTERFACE:${aws_sdk_SOURCE_DIR}/aws-cpp-sdk-core/include>"
    "$<BUILD_INTERFACE:${aws_sdk_SOURCE_DIR}/src/aws-cpp-sdk-core/include>"
    "$<BUILD_INTERFACE:${aws_sdk_BINARY_DIR}/generated/src/aws-cpp-sdk-core/include>"
    "$<BUILD_INTERFACE:${_aws_crt_root}/include>"
    "$<BUILD_INTERFACE:${libcurl_SOURCE_DIR}/include>"
)

if(POLICY CMP0169)
    cmake_policy(POP)
endif()

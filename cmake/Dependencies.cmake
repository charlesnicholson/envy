include(FetchContent)
include(ExternalProject)

if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()

set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "Disable deprecated CMake warnings from third-party builds" FORCE)

set_property(GLOBAL PROPERTY JOB_POOLS codex_fetch=4)

# Keep all dependency artifacts under the active build directory so removing it
# (e.g., rm -rf out/) leaves the system pristine.
set(FETCHCONTENT_BASE_DIR "${CMAKE_BINARY_DIR}/_deps")

find_package(ZLIB REQUIRED)
find_library(RESOLV_LIBRARY resolv REQUIRED)

# Enforce static libraries across third-party builds.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build dependencies as static libraries" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "Disable dependency test targets" FORCE)

# libgit2 -------------------------------------------------------------------
set(USE_HTTPS SecureTransport CACHE STRING "" FORCE)
set(USE_SSH ON CACHE BOOL "" FORCE)
set(LIBGIT2_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LIBGIT2_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LIBGIT2_BUILD_CLI OFF CACHE BOOL "" FORCE)
set(LIBGIT2_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(libgit2
    GIT_REPOSITORY https://github.com/libgit2/libgit2.git
    GIT_TAG v1.7.2
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(libgit2)
if(TARGET libgit2package)
    add_library(codex::libgit2 ALIAS libgit2package)
    target_include_directories(libgit2package INTERFACE
        ${libgit2_SOURCE_DIR}/include
        ${libgit2_BINARY_DIR}/include)
elseif(TARGET git2)
    add_library(codex::libgit2 ALIAS git2)
elseif(TARGET libgit2)
    add_library(codex::libgit2 ALIAS libgit2)
else()
    message(FATAL_ERROR "libgit2 target was not created by FetchContent")
endif()

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

# libcurl -------------------------------------------------------------------
set(CURL_USE_OPENSSL OFF CACHE BOOL "" FORCE)
set(CURL_USE_SECTRANSP ON CACHE BOOL "" FORCE)
set(CURL_ZLIB ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_LDAP ON CACHE BOOL "" FORCE)
set(ENABLE_THREADED_RESOLVER ON CACHE BOOL "" FORCE)
set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
set(CURL_ENABLE_SSL ON CACHE BOOL "" FORCE)
set(CURL_CA_BUNDLE "auto" CACHE STRING "" FORCE)
FetchContent_Declare(libcurl
    GIT_REPOSITORY https://github.com/curl/curl.git
    GIT_TAG curl-8_8_0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(libcurl)
if(NOT TARGET CURL::libcurl)
    add_library(CURL::libcurl ALIAS libcurl)
endif()

# oneTBB --------------------------------------------------------------------
set(TBB_TEST OFF CACHE BOOL "" FORCE)
set(TBB_STRICT OFF CACHE BOOL "" FORCE)
FetchContent_Declare(oneTBB
    GIT_REPOSITORY https://github.com/oneapi-src/oneTBB.git
    GIT_TAG v2022.0.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(oneTBB)

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
set(ENABLE_OPENSSL OFF CACHE BOOL "" FORCE)
set(ENABLE_EXPAT OFF CACHE BOOL "" FORCE)
set(ENABLE_CNG OFF CACHE BOOL "" FORCE)
set(ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(LIBARCHIVE_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(libarchive
    GIT_REPOSITORY https://github.com/libarchive/libarchive.git
    GIT_TAG v3.7.2
    GIT_SHALLOW TRUE
)
FetchContent_GetProperties(libarchive)
if(NOT libarchive_POPULATED)
    FetchContent_Populate(libarchive)
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
    GIT_REPOSITORY https://github.com/BLAKE3-team/BLAKE3.git
    GIT_TAG 1.5.1
    GIT_SHALLOW TRUE
)
FetchContent_GetProperties(blake3)
if(NOT blake3_POPULATED)
    FetchContent_Populate(blake3)
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
    add_library(blake3::blake3 ALIAS blake3)
endif()

# mbedTLS -------------------------------------------------------------------
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ENABLE_SHARED OFF CACHE BOOL "" FORCE)
set(ENABLE_STATIC ON CACHE BOOL "" FORCE)
FetchContent_Declare(mbedtls
    GIT_REPOSITORY https://github.com/Mbed-TLS/mbedtls.git
    GIT_TAG v3.5.2
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(mbedtls)
set(MBEDTLS_INCLUDE_DIR "${mbedtls_SOURCE_DIR}/include" CACHE PATH "" FORCE)
set(MBEDTLS_LIBRARY_DIR "${mbedtls_BINARY_DIR}/library" CACHE PATH "" FORCE)
set(MBEDTLS_LIBRARY "${MBEDTLS_LIBRARY_DIR}/libmbedtls.a" CACHE FILEPATH "" FORCE)
set(MBEDX509_LIBRARY "${MBEDTLS_LIBRARY_DIR}/libmbedx509.a" CACHE FILEPATH "" FORCE)
set(MBEDCRYPTO_LIBRARY "${MBEDTLS_LIBRARY_DIR}/libmbedcrypto.a" CACHE FILEPATH "" FORCE)
add_library(mbedtls_static INTERFACE)
target_include_directories(mbedtls_static INTERFACE "${MBEDTLS_INCLUDE_DIR}")
target_link_libraries(mbedtls_static INTERFACE
    "${MBEDTLS_LIBRARY}"
    "${MBEDX509_LIBRARY}"
    "${MBEDCRYPTO_LIBRARY}")
add_library(mbedtls::bundle ALIAS mbedtls_static)

# libssh2 -------------------------------------------------------------------
set(LIBSSH2_WITH_MBEDTLS ON CACHE BOOL "" FORCE)
set(LIBSSH2_WITH_OPENSSL OFF CACHE BOOL "" FORCE)
set(LIBSSH2_WITH_LIBGCRYPT OFF CACHE BOOL "" FORCE)
set(LIBSSH2_WITH_WINCNG OFF CACHE BOOL "" FORCE)
set(ENABLE_ZLIB_COMPRESSION ON CACHE BOOL "" FORCE)
set(LIBSSH2_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
FetchContent_Declare(libssh2
    GIT_REPOSITORY https://github.com/libssh2/libssh2.git
    GIT_TAG libssh2-1.11.0
    GIT_SHALLOW TRUE
)
FetchContent_GetProperties(libssh2)
if(NOT libssh2_POPULATED)
    FetchContent_Populate(libssh2)
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
if(TARGET libssh2_static)
    add_library(libssh2::libssh2 ALIAS libssh2_static)
elseif(TARGET libssh2)
    add_library(libssh2::libssh2 ALIAS libssh2)
else()
    message(FATAL_ERROR "libssh2 target was not created by FetchContent")
endif()

# Lua -----------------------------------------------------------------------
FetchContent_Declare(lua
    GIT_REPOSITORY https://github.com/lua/lua.git
    GIT_TAG v5.4.6
    GIT_SHALLOW TRUE
)
FetchContent_GetProperties(lua)
if(NOT lua_POPULATED)
    FetchContent_Populate(lua)
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
    add_library(lua::lua ALIAS lua)
endif()

# Aggregate -----------------------------------------------------------------
add_library(codex_thirdparty INTERFACE)
add_library(codex::thirdparty ALIAS codex_thirdparty)

target_link_libraries(codex_thirdparty
    INTERFACE
        codex::libgit2
        CURL::libcurl
        TBB::tbb
        libarchive::libarchive
        lua::lua
        blake3::blake3
        mbedtls::bundle
        libssh2::libssh2
        ZLIB::ZLIB
        ${RESOLV_LIBRARY}
)

target_compile_definitions(codex_thirdparty INTERFACE
    CURL_STATICLIB
)

target_include_directories(codex_thirdparty INTERFACE
    "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
)

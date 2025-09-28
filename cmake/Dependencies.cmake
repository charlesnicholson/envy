include(FetchContent)
include(ExternalProject)

set_property(GLOBAL PROPERTY JOB_POOLS codex_fetch=4)

# Keep all dependency artifacts under the active build directory so removing it
# (e.g., rm -rf out/) leaves the system pristine.
set(FETCHCONTENT_BASE_DIR "${CMAKE_BINARY_DIR}/_deps")

set(OPENSSL_ROOT_DIR "/opt/homebrew/opt/openssl@3")
set(OPENSSL_USE_STATIC_LIBS ON)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)
find_package(ZLIB REQUIRED)
find_library(RESOLV_LIBRARY resolv REQUIRED)

# Enforce static libraries across third-party builds.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build dependencies as static libraries" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "Disable dependency test targets" FORCE)

# libgit2 -------------------------------------------------------------------
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

# libcurl -------------------------------------------------------------------
set(CURL_USE_OPENSSL ON CACHE BOOL "" FORCE)
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

# OpenSSH -------------------------------------------------------------------
set(OPENSSH_VERSION "9.7p1")
set(OPENSSH_PREFIX "${CMAKE_BINARY_DIR}/third_party/openssh")
set(OPENSSH_SOURCE_DIR "${CMAKE_BINARY_DIR}/third_party/openssh-src")
set(OPENSSH_BINARY_DIR "${CMAKE_BINARY_DIR}/third_party/openssh-build")
set(OPENSSH_SSL_ROOT "/opt/homebrew/opt/openssl@3")
ExternalProject_Add(openssh_ep
    URL https://cdn.openbsd.org/pub/OpenBSD/OpenSSH/portable/openssh-${OPENSSH_VERSION}.tar.gz
    URL_HASH SHA256=490426f766d82a2763fcacd8d83ea3d70798750c7bd2aff2e57dc5660f773ffd
    SOURCE_DIR ${OPENSSH_SOURCE_DIR}
    BINARY_DIR ${OPENSSH_BINARY_DIR}
    PATCH_COMMAND ${CMAKE_COMMAND} -DOPENSSH_SOURCE_DIR=${OPENSSH_SOURCE_DIR} -P ${PROJECT_SOURCE_DIR}/cmake/PatchOpenSSH.cmake
    CONFIGURE_COMMAND ${CMAKE_COMMAND} -E env
        CPPFLAGS=-I${OPENSSH_SSL_ROOT}/include
        LDFLAGS=-L${OPENSSH_SSL_ROOT}/lib
        PKG_CONFIG_PATH=${OPENSSH_SSL_ROOT}/lib/pkgconfig
        ${OPENSSH_SOURCE_DIR}/configure
            --prefix=${OPENSSH_PREFIX}
            --disable-etc-default-login
            --without-zlib-version-check
            --without-openssl-header-check
            --with-ssl-dir=${OPENSSH_SSL_ROOT}
            --with-ssl-engine
            --disable-security-key
            --without-security-key-builtin
            --without-security-key-provider
            --with-privsep-path=${OPENSSH_PREFIX}/var/empty
    BUILD_COMMAND /bin/sh -c "make clean && make"
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS
        ${OPENSSH_BINARY_DIR}/libssh.a
        ${OPENSSH_BINARY_DIR}/openbsd-compat/libopenbsd-compat.a
    LOG_CONFIGURE ON
    LOG_BUILD ON
)

add_library(openssh STATIC IMPORTED)
set_target_properties(openssh PROPERTIES
    IMPORTED_LOCATION "${OPENSSH_BINARY_DIR}/libssh.a"
    INTERFACE_INCLUDE_DIRECTORIES "${OPENSSH_SOURCE_DIR};${OPENSSH_SOURCE_DIR}/openbsd-compat;${OPENSSH_BINARY_DIR}"
    INTERFACE_COMPILE_DEFINITIONS HAVE_CONFIG_H
)
set_property(TARGET openssh APPEND PROPERTY INTERFACE_LINK_LIBRARIES
    "${OPENSSH_BINARY_DIR}/openbsd-compat/libopenbsd-compat.a")
add_dependencies(openssh openssh_ep)
add_library(openssh::ssh ALIAS openssh)

# Aggregate -----------------------------------------------------------------
add_library(codex_thirdparty INTERFACE)
add_library(codex::thirdparty ALIAS codex_thirdparty)

target_link_libraries(codex_thirdparty
    INTERFACE
        codex_openssh_stubs
        codex::libgit2
        CURL::libcurl
        TBB::tbb
        libarchive::libarchive
        lua::lua
        openssh::ssh
        blake3::blake3
        OpenSSL::Crypto
        ZLIB::ZLIB
        ${RESOLV_LIBRARY}
)

target_compile_definitions(codex_thirdparty INTERFACE
    GIT_SSH
    CURL_STATICLIB
)

target_include_directories(codex_thirdparty INTERFACE
    "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
)

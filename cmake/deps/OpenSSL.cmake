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
    FetchContent_Populate(openssl)
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

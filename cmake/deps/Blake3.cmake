cmake_path(APPEND CODEX_THIRDPARTY_CACHE_DIR "${CODEX_BLAKE3_ARCHIVE}" OUTPUT_VARIABLE _blake3_archive)
set(_blake3_url "${CODEX_BLAKE3_URL}")
if(EXISTS "${_blake3_archive}")
    file(TO_CMAKE_PATH "${_blake3_archive}" _blake3_archive_norm)
    set(_blake3_url "file://${_blake3_archive_norm}")
endif()
FetchContent_Declare(blake3
    URL ${_blake3_url}
    URL_HASH SHA256=${CODEX_BLAKE3_SHA256}
)
FetchContent_GetProperties(blake3)
if(NOT blake3_POPULATED)
    FetchContent_Populate(blake3)
    FetchContent_GetProperties(blake3)
endif()

if(NOT TARGET blake3::blake3)
    set(BLAKE3_SOURCES
        "${blake3_SOURCE_DIR}/c/blake3.c"
        "${blake3_SOURCE_DIR}/c/blake3_dispatch.c"
        "${blake3_SOURCE_DIR}/c/blake3_portable.c"
    )
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "(x86_64|AMD64|amd64)")
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
unset(_blake3_archive)
unset(_blake3_archive_norm)
unset(_blake3_url)

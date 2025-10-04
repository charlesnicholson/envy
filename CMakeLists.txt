cmake_minimum_required(VERSION 4.1.2)

project(codex_cmake_test
    VERSION 0.1.0
    DESCRIPTION "Static test driver bundling libgit2, libcurl (OpenSSL), libssh2, OpenSSL, Lua, oneTBB, libarchive, and BLAKE3"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Favor size-optimized release binaries by normalizing optimization flags
set(_codex_size_opt "-Os")
if(MSVC)
    set(_codex_size_opt "/Os")
endif()

foreach(_codex_lang IN ITEMS C CXX)
    set(_codex_flags "${CMAKE_${_codex_lang}_FLAGS_RELEASE}")
    string(REGEX REPLACE "-O[0-9A-Za-z]*" "" _codex_flags "${_codex_flags}")
    string(REGEX REPLACE "/O[0-9A-Za-z]*" "" _codex_flags "${_codex_flags}")
    string(STRIP "${_codex_flags}" _codex_flags)
    if(_codex_flags STREQUAL "")
        set(_codex_flags "${_codex_size_opt}")
    else()
        string(APPEND _codex_flags " ${_codex_size_opt}")
    endif()
    set(CMAKE_${_codex_lang}_FLAGS_RELEASE "${_codex_flags}" CACHE STRING "Release flags" FORCE)
endforeach()
unset(_codex_size_opt)
unset(_codex_flags)
unset(_codex_lang)

option(ENABLE_LTO "Enable interprocedural optimization" ON)
if(ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_supported OUTPUT ipo_output)
    if(ipo_supported)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
    else()
        message(WARNING "IPO/LTO not supported: ${ipo_output}")
    endif()
endif()

include(cmake/Dependencies.cmake)

add_executable(codex_cmake_test
    src/main.cpp
)

set_target_properties(codex_cmake_test PROPERTIES
    OUTPUT_NAME codex-tool
)

target_compile_features(codex_cmake_test PRIVATE cxx_std_20)
target_compile_options(codex_cmake_test PRIVATE -ffunction-sections -fdata-sections)
target_link_options(codex_cmake_test PRIVATE -Wl,-dead_strip)

target_link_libraries(codex_cmake_test
    PRIVATE
        codex::thirdparty
)

add_dependencies(codex_cmake_test openssl)

target_include_directories(codex_cmake_test
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

cmake_minimum_required(VERSION 4.1.2)

project(envy_cmake_test
    VERSION 0.1.0
    DESCRIPTION "Static test driver bundling libgit2, libcurl (OpenSSL), libssh2, OpenSSL, Lua, oneTBB, libarchive, and BLAKE3"
    LANGUAGES C CXX)

if(POLICY CMP0069)
    cmake_policy(SET CMP0069 NEW)
    set(CMAKE_POLICY_DEFAULT_CMP0069 NEW)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_compile_definitions(_GNU_SOURCE)
    set(_envy_needs_link_groups ON)
endif()

# Favor size-optimized release binaries by normalizing optimization flags
set(_envy_size_opt "-Os")
if(MSVC)
    set(_envy_size_opt "/Os")
endif()

foreach(_envy_lang IN ITEMS C CXX)
    set(_envy_flags "${CMAKE_${_envy_lang}_FLAGS_RELEASE}")
    string(REGEX REPLACE "-O[0-9A-Za-z]*" "" _envy_flags "${_envy_flags}")
    string(REGEX REPLACE "/O[0-9A-Za-z]*" "" _envy_flags "${_envy_flags}")
    string(STRIP "${_envy_flags}" _envy_flags)
    if(_envy_flags STREQUAL "")
        set(_envy_flags "${_envy_size_opt}")
    else()
        string(APPEND _envy_flags " ${_envy_size_opt}")
    endif()
    set(CMAKE_${_envy_lang}_FLAGS_RELEASE "${_envy_flags}" CACHE STRING "Release flags" FORCE)
endforeach()
unset(_envy_size_opt)
unset(_envy_flags)
unset(_envy_lang)

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

add_executable(envy_cmake_test)

target_sources(envy_cmake_test
    PRIVATE
        src/main.cpp
    PUBLIC
        FILE_SET public_headers TYPE HEADERS
            BASE_DIRS "${PROJECT_SOURCE_DIR}/include"
            FILES
                include/lua.hpp
)

set_target_properties(envy_cmake_test PROPERTIES
    OUTPUT_NAME envy
)

target_compile_features(envy_cmake_test PRIVATE cxx_std_20)
target_compile_options(envy_cmake_test PRIVATE
    $<$<COMPILE_LANG_AND_ID:CXX,AppleClang,Clang,GNU>:-ffunction-sections>
    $<$<COMPILE_LANG_AND_ID:CXX,AppleClang,Clang,GNU>:-fdata-sections>
    $<$<COMPILE_LANG_AND_ID:C,AppleClang,Clang,GNU>:-ffunction-sections>
    $<$<COMPILE_LANG_AND_ID:C,AppleClang,Clang,GNU>:-fdata-sections>
)
target_link_options(envy_cmake_test PRIVATE
    $<$<PLATFORM_ID:Darwin>:-Wl,-dead_strip>
)

if(_envy_needs_link_groups)
    target_link_libraries(envy_cmake_test PRIVATE "-Wl,--start-group")
endif()

target_link_libraries(envy_cmake_test
    PRIVATE
        envy::thirdparty
        libssh2::libssh2
)

if(_envy_needs_link_groups)
    target_link_libraries(envy_cmake_test PRIVATE "-Wl,--end-group")
endif()

unset(_envy_needs_link_groups)

add_dependencies(envy_cmake_test openssl)

target_include_directories(envy_cmake_test
    PRIVATE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
)

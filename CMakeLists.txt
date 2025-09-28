cmake_minimum_required(VERSION 3.26)

if(POLICY CMP0069)
    cmake_policy(SET CMP0069 NEW)
    set(CMAKE_POLICY_DEFAULT_CMP0069 NEW)
endif()

project(codex_cmake_test
    VERSION 0.1.0
    DESCRIPTION "Static test driver bundling libgit2, libcurl (OpenSSL), libssh2, OpenSSL, Lua, oneTBB, libarchive, and BLAKE3"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

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

target_link_libraries(codex_cmake_test
    PRIVATE
        codex::thirdparty
)

add_dependencies(codex_cmake_test openssl)

target_include_directories(codex_cmake_test
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

set(BUILD_TESTING ON CACHE BOOL "Enable project tests" FORCE)
include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

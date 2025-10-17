cmake_path(APPEND ENVY_CACHE_DIR "cli11-src" OUTPUT_VARIABLE cli11_SOURCE_DIR)
file(MAKE_DIRECTORY "${cli11_SOURCE_DIR}")

set(_cli11_local_file "${cli11_SOURCE_DIR}/CLI11.hpp")
set(_cli11_url "${ENVY_CLI11_URL}")

if(EXISTS "${_cli11_local_file}")
    file(TO_CMAKE_PATH "${_cli11_local_file}" _cli11_file_norm)
    set(_cli11_url "file://${_cli11_file_norm}")
endif()

if(NOT EXISTS "${_cli11_local_file}")
    file(DOWNLOAD
        "${_cli11_url}"
        "${_cli11_local_file}"
        EXPECTED_HASH SHA256=${ENVY_CLI11_SHA256}
        SHOW_PROGRESS
    )
endif()

if(NOT TARGET CLI11::CLI11)
    add_library(cli11 INTERFACE)
    target_include_directories(cli11 INTERFACE "${cli11_SOURCE_DIR}")
    add_library(CLI11::CLI11 ALIAS cli11)
endif()

unset(_cli11_local_file)
unset(_cli11_url)
unset(_cli11_file_norm)

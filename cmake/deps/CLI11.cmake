cmake_path(APPEND ENVY_CACHE_DIR "cli11-src" OUTPUT_VARIABLE cli11_SOURCE_DIR)
file(MAKE_DIRECTORY "${cli11_SOURCE_DIR}")

envy_fetch_verified_file(
    PATH "${cli11_SOURCE_DIR}/CLI11.hpp"
    URL "${ENVY_CLI11_URL}"
    SHA256 "${ENVY_CLI11_SHA256}"
    HUMAN_NAME "CLI11 ${ENVY_CLI11_VERSION}"
    SHOW_PROGRESS
)

if(NOT TARGET CLI11::CLI11)
    add_library(cli11 INTERFACE)
    target_include_directories(cli11 INTERFACE "${cli11_SOURCE_DIR}")
    add_library(CLI11::CLI11 ALIAS cli11)
endif()

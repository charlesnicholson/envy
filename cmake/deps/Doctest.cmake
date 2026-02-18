# Doctest - C++ testing framework (single-header amalgamation)
# https://github.com/doctest/doctest

set(DOCTEST_VERSION "2.4.12")
set(DOCTEST_URL "https://github.com/doctest/doctest/releases/download/v${DOCTEST_VERSION}/doctest.h")
set(DOCTEST_SHA256 "2a31654ead2a6e5ef93086ca97659b701710c80275207b3bdb12676c012daceb")
set(DOCTEST_CACHE_DIR "${ENVY_CACHE_DIR}/doctest-src")

# Ensure cache directory exists
file(MAKE_DIRECTORY "${DOCTEST_CACHE_DIR}")

set(DOCTEST_HEADER_PATH "${DOCTEST_CACHE_DIR}/doctest.h")

# Download if not cached
if(NOT EXISTS "${DOCTEST_HEADER_PATH}")
  message(STATUS "Downloading doctest ${DOCTEST_VERSION}...")
  file(DOWNLOAD
    "${DOCTEST_URL}"
    "${DOCTEST_HEADER_PATH}"
    EXPECTED_HASH SHA256=${DOCTEST_SHA256}
    SHOW_PROGRESS
  )
  message(STATUS "Doctest cached at ${DOCTEST_HEADER_PATH}")
else()
  message(STATUS "Using cached doctest at ${DOCTEST_HEADER_PATH}")
endif()

# Create interface library
add_library(doctest INTERFACE)
target_include_directories(doctest INTERFACE "${DOCTEST_CACHE_DIR}")

# Export for consumers
add_library(doctest::doctest ALIAS doctest)

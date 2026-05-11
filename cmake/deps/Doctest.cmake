# Doctest - C++ testing framework (single-header amalgamation)
# https://github.com/doctest/doctest

set(DOCTEST_CACHE_DIR "${ENVY_CACHE_DIR}/doctest-src")
file(MAKE_DIRECTORY "${DOCTEST_CACHE_DIR}")

set(_doctest_local_file "${DOCTEST_CACHE_DIR}/doctest.h")

if(NOT EXISTS "${_doctest_local_file}")
  message(STATUS "Downloading doctest ${ENVY_DOCTEST_VERSION}...")
  file(DOWNLOAD
    "${ENVY_DOCTEST_URL}"
    "${_doctest_local_file}"
    EXPECTED_HASH SHA256=${ENVY_DOCTEST_SHA256}
    SHOW_PROGRESS
  )
  message(STATUS "Doctest cached at ${_doctest_local_file}")
else()
  message(STATUS "Using cached doctest at ${_doctest_local_file}")
endif()

if(NOT TARGET doctest::doctest)
  add_library(doctest INTERFACE)
  target_include_directories(doctest INTERFACE "${DOCTEST_CACHE_DIR}")
  add_library(doctest::doctest ALIAS doctest)
endif()

unset(_doctest_local_file)

# Doctest - C++ testing framework (single-header amalgamation)
# https://github.com/doctest/doctest

set(DOCTEST_CACHE_DIR "${ENVY_CACHE_DIR}/doctest-src")
file(MAKE_DIRECTORY "${DOCTEST_CACHE_DIR}")

set(_doctest_local_file "${DOCTEST_CACHE_DIR}/doctest.h")

# Hash-verify any cached copy so a stale header from a previous version
# (e.g. restored via CI's `restore-keys` fallback) is replaced instead of reused.
if(EXISTS "${_doctest_local_file}")
  file(SHA256 "${_doctest_local_file}" _doctest_cached_hash)
  if(NOT _doctest_cached_hash STREQUAL ENVY_DOCTEST_SHA256)
    file(REMOVE "${_doctest_local_file}")
  endif()
  unset(_doctest_cached_hash)
endif()

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

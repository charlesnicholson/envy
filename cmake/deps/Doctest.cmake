# Doctest - C++ testing framework (single-header amalgamation)
# https://github.com/doctest/doctest

set(DOCTEST_CACHE_DIR "${ENVY_CACHE_DIR}/doctest-src")
file(MAKE_DIRECTORY "${DOCTEST_CACHE_DIR}")

envy_fetch_verified_file(
  PATH "${DOCTEST_CACHE_DIR}/doctest.h"
  URL "${ENVY_DOCTEST_URL}"
  SHA256 "${ENVY_DOCTEST_SHA256}"
  HUMAN_NAME "doctest ${ENVY_DOCTEST_VERSION}"
  SHOW_PROGRESS
)

if(NOT TARGET doctest::doctest)
  add_library(doctest INTERFACE)
  target_include_directories(doctest INTERFACE "${DOCTEST_CACHE_DIR}")
  add_library(doctest::doctest ALIAS doctest)
endif()

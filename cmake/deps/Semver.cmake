# Semver - Semantic Versioning library (single-header)
# https://github.com/Neargye/semver

set(SEMVER_CACHE_DIR "${ENVY_CACHE_DIR}/semver-src")
file(MAKE_DIRECTORY "${SEMVER_CACHE_DIR}")

set(_semver_local_file "${SEMVER_CACHE_DIR}/semver.hpp")
set(_semver_license_file "${SEMVER_CACHE_DIR}/LICENSE")

if(NOT EXISTS "${_semver_local_file}")
  message(STATUS "Downloading semver ${ENVY_SEMVER_VERSION}...")
  file(DOWNLOAD
    "${ENVY_SEMVER_URL}"
    "${_semver_local_file}"
    EXPECTED_HASH SHA256=${ENVY_SEMVER_SHA256}
    SHOW_PROGRESS
  )
  message(STATUS "Semver cached at ${_semver_local_file}")
else()
  message(STATUS "Using cached semver at ${_semver_local_file}")
endif()

if(NOT EXISTS "${_semver_license_file}")
  file(DOWNLOAD
    "${ENVY_SEMVER_LICENSE_URL}"
    "${_semver_license_file}"
    EXPECTED_HASH SHA256=${ENVY_SEMVER_LICENSE_SHA256}
  )
endif()

if(NOT TARGET semver::semver)
  add_library(semver INTERFACE)
  target_include_directories(semver INTERFACE "${SEMVER_CACHE_DIR}")
  add_library(semver::semver ALIAS semver)
endif()

unset(_semver_local_file)
unset(_semver_license_file)

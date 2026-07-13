# Semver - Semantic Versioning library (single-header)
# https://github.com/Neargye/semver

set(SEMVER_CACHE_DIR "${ENVY_CACHE_DIR}/semver-src")
file(MAKE_DIRECTORY "${SEMVER_CACHE_DIR}")

envy_fetch_verified_file(
  PATH "${SEMVER_CACHE_DIR}/semver.hpp"
  URL "${ENVY_SEMVER_URL}"
  SHA256 "${ENVY_SEMVER_SHA256}"
  HUMAN_NAME "semver ${ENVY_SEMVER_VERSION}"
  SHOW_PROGRESS
)
envy_fetch_verified_file(
  PATH "${SEMVER_CACHE_DIR}/LICENSE"
  URL "${ENVY_SEMVER_LICENSE_URL}"
  SHA256 "${ENVY_SEMVER_LICENSE_SHA256}"
)

if(NOT TARGET semver::semver)
  add_library(semver INTERFACE)
  target_include_directories(semver INTERFACE "${SEMVER_CACHE_DIR}")
  add_library(semver::semver ALIAS semver)
endif()

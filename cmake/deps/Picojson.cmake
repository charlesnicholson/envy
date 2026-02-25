# picojson - JSON parser/serializer (single-header)
# https://github.com/kazuho/picojson

set(PICOJSON_CACHE_DIR "${ENVY_CACHE_DIR}/picojson-src")
file(MAKE_DIRECTORY "${PICOJSON_CACHE_DIR}")

set(_picojson_local_file "${PICOJSON_CACHE_DIR}/picojson.h")
set(_picojson_license_file "${PICOJSON_CACHE_DIR}/LICENSE")

if(NOT EXISTS "${_picojson_local_file}")
  message(STATUS "Downloading picojson ${ENVY_PICOJSON_VERSION}...")
  file(DOWNLOAD
    "${ENVY_PICOJSON_URL}"
    "${_picojson_local_file}"
    EXPECTED_HASH SHA256=${ENVY_PICOJSON_SHA256}
    SHOW_PROGRESS
  )
  message(STATUS "picojson cached at ${_picojson_local_file}")
else()
  message(STATUS "Using cached picojson at ${_picojson_local_file}")
endif()

if(NOT EXISTS "${_picojson_license_file}")
  file(DOWNLOAD
    "${ENVY_PICOJSON_LICENSE_URL}"
    "${_picojson_license_file}"
    EXPECTED_HASH SHA256=${ENVY_PICOJSON_LICENSE_SHA256}
  )
endif()

if(NOT TARGET picojson::picojson)
  add_library(picojson INTERFACE)
  target_include_directories(picojson INTERFACE "${PICOJSON_CACHE_DIR}")
  target_compile_definitions(picojson INTERFACE
    ENVY_PICOJSON_VERSION="${ENVY_PICOJSON_VERSION}")
  add_library(picojson::picojson ALIAS picojson)
endif()

unset(_picojson_local_file)
unset(_picojson_license_file)

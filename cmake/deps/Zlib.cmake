cmake_path(APPEND ENVY_THIRDPARTY_CACHE_DIR "${ENVY_ZLIB_ARCHIVE}" OUTPUT_VARIABLE _zlib_archive)
set(_zlib_url "${ENVY_ZLIB_URL}")
if(EXISTS "${_zlib_archive}")
    file(TO_CMAKE_PATH "${_zlib_archive}" _zlib_archive_norm)
    set(_zlib_url "file://${_zlib_archive_norm}")
endif()

set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(envy_zlib
    URL ${_zlib_url}
    URL_HASH SHA256=${ENVY_ZLIB_SHA256}
)
FetchContent_MakeAvailable(envy_zlib)
FetchContent_GetProperties(envy_zlib)

if(DEFINED envy_zlib_SOURCE_DIR)
    set(ZLIB_INCLUDE_DIR "${envy_zlib_SOURCE_DIR}" CACHE PATH "" FORCE)
    set(ZLIB_INCLUDE_DIRS "${envy_zlib_SOURCE_DIR}" CACHE STRING "" FORCE)
endif()

if(DEFINED envy_zlib_BINARY_DIR)
    set(_envy_zlib_primary_target "")
    if(TARGET zlibstatic)
        set(_envy_zlib_primary_target zlibstatic)
    elseif(TARGET zlib)
        set(_envy_zlib_primary_target zlib)
    endif()

    if(_envy_zlib_primary_target STREQUAL "")
        message(FATAL_ERROR "Failed to locate zlib target after FetchContent_MakeAvailable")
    endif()

    get_target_property(_envy_zlib_output_name ${_envy_zlib_primary_target} OUTPUT_NAME)
    if(NOT _envy_zlib_output_name)
        set(_envy_zlib_output_name z)
    endif()

    cmake_path(APPEND envy_zlib_BINARY_DIR
        "${CMAKE_STATIC_LIBRARY_PREFIX}${_envy_zlib_output_name}${CMAKE_STATIC_LIBRARY_SUFFIX}"
        OUTPUT_VARIABLE _envy_zlib_archive_path)

    set(ZLIB_LIBRARY "${_envy_zlib_archive_path}" CACHE FILEPATH "" FORCE)
    if(NOT TARGET ZLIB::ZLIB)
        add_library(ZLIB::ZLIB STATIC IMPORTED GLOBAL)
    endif()
    set_target_properties(ZLIB::ZLIB PROPERTIES
        IMPORTED_LOCATION "${_envy_zlib_archive_path}"
        INTERFACE_INCLUDE_DIRECTORIES "${envy_zlib_SOURCE_DIR}")

    set(ZLIB_LIBRARIES ZLIB::ZLIB CACHE STRING "" FORCE)
    set(ZLIB_VERSION_STRING "${ENVY_ZLIB_VERSION}" CACHE STRING "" FORCE)
endif()

unset(_envy_zlib_primary_target)
unset(_envy_zlib_output_name)
unset(_envy_zlib_archive_path)
unset(_zlib_archive)
unset(_zlib_archive_norm)
unset(_zlib_url)

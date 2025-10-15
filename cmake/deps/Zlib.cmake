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

    unset(ZLIB_LIBRARY CACHE)
    unset(ZLIB_LIBRARIES CACHE)

    set(ZLIB_LIBRARY "$<TARGET_FILE:${_envy_zlib_primary_target}>" CACHE STRING "" FORCE)
    if(DEFINED envy_zlib_SOURCE_DIR)
        set(ZLIB_INCLUDE_DIR "${envy_zlib_SOURCE_DIR}" CACHE PATH "" FORCE)
        set(ZLIB_INCLUDE_DIRS "${envy_zlib_SOURCE_DIR};${envy_zlib_BINARY_DIR}" CACHE STRING "" FORCE)
    endif()

    if(NOT TARGET ZLIB::ZLIB)
        add_library(ZLIB::ZLIB INTERFACE IMPORTED)
    endif()
    set(_envy_zlib_include_paths "")
    if(DEFINED envy_zlib_SOURCE_DIR)
        list(APPEND _envy_zlib_include_paths "${envy_zlib_SOURCE_DIR}")
    endif()
    list(APPEND _envy_zlib_include_paths "${envy_zlib_BINARY_DIR}")

    set_target_properties(ZLIB::ZLIB PROPERTIES
        INTERFACE_LINK_LIBRARIES ${_envy_zlib_primary_target}
        INTERFACE_INCLUDE_DIRECTORIES "${_envy_zlib_include_paths}")

    set(ZLIB_LIBRARIES "${ZLIB_LIBRARY}" CACHE STRING "" FORCE)
    set(ZLIB_VERSION_STRING "${ENVY_ZLIB_VERSION}" CACHE STRING "" FORCE)

    if(DEFINED envy_zlib_SOURCE_DIR AND EXISTS "${envy_zlib_BINARY_DIR}/zconf.h")
        configure_file("${envy_zlib_BINARY_DIR}/zconf.h" "${envy_zlib_SOURCE_DIR}/zconf.h" COPYONLY)
    endif()
endif()

unset(_envy_zlib_include_paths)
unset(_envy_zlib_primary_target)
unset(_envy_zlib_output_name)
unset(_zlib_archive)
unset(_zlib_archive_norm)
unset(_zlib_url)

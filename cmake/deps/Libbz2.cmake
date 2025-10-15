cmake_path(APPEND ENVY_THIRDPARTY_CACHE_DIR "${ENVY_LIBBZ2_ARCHIVE}" OUTPUT_VARIABLE _libbz2_archive)
set(_libbz2_url "${ENVY_LIBBZ2_URL}")
if(EXISTS "${_libbz2_archive}")
    file(TO_CMAKE_PATH "${_libbz2_archive}" _libbz2_archive_norm)
    set(_libbz2_url "file://${_libbz2_archive_norm}")
endif()

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(envy_libbz2
    URL ${_libbz2_url}
    URL_HASH SHA256=${ENVY_LIBBZ2_SHA256}
)
FetchContent_MakeAvailable(envy_libbz2)
FetchContent_GetProperties(envy_libbz2)

if(DEFINED envy_libbz2_BINARY_DIR)
    set(_envy_libbz2_primary_target "")
    if(TARGET BZip2::BZip2)
        set(_envy_libbz2_primary_target BZip2::BZip2)
    elseif(TARGET bz2_static)
        set(_envy_libbz2_primary_target bz2_static)
    elseif(TARGET bz2)
        set(_envy_libbz2_primary_target bz2)
    elseif(TARGET libbz2)
        set(_envy_libbz2_primary_target libbz2)
    endif()

    if(_envy_libbz2_primary_target STREQUAL "")
        message(FATAL_ERROR "Failed to locate libbz2 target after FetchContent_MakeAvailable")
    endif()

    unset(BZIP2_LIBRARY CACHE)
    unset(BZIP2_LIBRARIES CACHE)

    set(BZIP2_LIBRARY "$<TARGET_FILE:${_envy_libbz2_primary_target}>" CACHE STRING "" FORCE)
    if(DEFINED envy_libbz2_SOURCE_DIR)
        set(BZIP2_INCLUDE_DIR "${envy_libbz2_SOURCE_DIR}" CACHE PATH "" FORCE)
        set(BZIP2_INCLUDE_DIRS "${BZIP2_INCLUDE_DIR}" CACHE STRING "" FORCE)
    endif()

    if(NOT TARGET BZip2::BZip2)
        add_library(BZip2::BZip2 INTERFACE IMPORTED)
    endif()
    set(_envy_libbz2_include_paths "")
    if(DEFINED envy_libbz2_SOURCE_DIR)
        list(APPEND _envy_libbz2_include_paths "${envy_libbz2_SOURCE_DIR}")
    endif()

    set_target_properties(BZip2::BZip2 PROPERTIES
        INTERFACE_LINK_LIBRARIES ${_envy_libbz2_primary_target}
        INTERFACE_INCLUDE_DIRECTORIES "${_envy_libbz2_include_paths}")

    set(BZIP2_LIBRARIES "${BZIP2_LIBRARY}" CACHE STRING "" FORCE)
    set(BZIP2_VERSION_STRING "${ENVY_LIBBZ2_VERSION}" CACHE STRING "" FORCE)
    set(BZIP2_FOUND TRUE CACHE BOOL "" FORCE)
    set(BZip2_FOUND TRUE CACHE BOOL "" FORCE)
endif()

unset(_envy_libbz2_include_paths)
unset(_envy_libbz2_primary_target)
unset(_libbz2_archive)
unset(_libbz2_archive_norm)
unset(_libbz2_url)

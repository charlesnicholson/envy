cmake_path(APPEND ENVY_THIRDPARTY_CACHE_DIR "${ENVY_LIBZSTD_ARCHIVE}" OUTPUT_VARIABLE _libzstd_archive)
set(_libzstd_url "${ENVY_LIBZSTD_URL}")
if(EXISTS "${_libzstd_archive}")
    file(TO_CMAKE_PATH "${_libzstd_archive}" _libzstd_archive_norm)
    set(_libzstd_url "file://${_libzstd_archive_norm}")
endif()

set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
set(ZSTD_LEGACY_SUPPORT OFF CACHE BOOL "" FORCE)

FetchContent_Declare(envy_libzstd
    URL ${_libzstd_url}
    URL_HASH SHA256=${ENVY_LIBZSTD_SHA256}
    SOURCE_SUBDIR build/cmake
)
FetchContent_MakeAvailable(envy_libzstd)
FetchContent_GetProperties(envy_libzstd)

if(DEFINED envy_libzstd_BINARY_DIR)
    set(_envy_libzstd_primary_target libzstd_static)

    unset(ZSTD_LIBRARY CACHE)
    unset(ZSTD_LIBRARIES CACHE)

    set(ZSTD_LIBRARY "${_envy_libzstd_primary_target}" CACHE STRING "" FORCE)
    if(DEFINED envy_libzstd_SOURCE_DIR)
        set(ZSTD_INCLUDE_DIR "${envy_libzstd_SOURCE_DIR}/lib" CACHE PATH "" FORCE)
        set(ZSTD_INCLUDE_DIRS "${ZSTD_INCLUDE_DIR}" CACHE STRING "" FORCE)
    endif()

    if(NOT TARGET Zstd::Zstd)
        add_library(Zstd::Zstd INTERFACE IMPORTED)
    endif()
    set(_envy_libzstd_include_paths "")
    if(DEFINED envy_libzstd_SOURCE_DIR)
        list(APPEND _envy_libzstd_include_paths "${envy_libzstd_SOURCE_DIR}/lib")
    endif()

    set_target_properties(Zstd::Zstd PROPERTIES
        INTERFACE_LINK_LIBRARIES ${_envy_libzstd_primary_target}
        INTERFACE_INCLUDE_DIRECTORIES "${_envy_libzstd_include_paths}")

    set(ZSTD_LIBRARIES "${ZSTD_LIBRARY}" CACHE STRING "" FORCE)
    set(ZSTD_VERSION_STRING "${ENVY_LIBZSTD_VERSION}" CACHE STRING "" FORCE)
    set(ZSTD_FOUND TRUE CACHE BOOL "" FORCE)
    set(Zstd_FOUND TRUE CACHE BOOL "" FORCE)
endif()

unset(_envy_libzstd_include_paths)
unset(_envy_libzstd_primary_target)
unset(_libzstd_archive)
unset(_libzstd_archive_norm)
unset(_libzstd_url)

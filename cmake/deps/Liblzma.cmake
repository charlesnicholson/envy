cmake_path(APPEND ENVY_THIRDPARTY_CACHE_DIR "${ENVY_LIBLZMA_ARCHIVE}" OUTPUT_VARIABLE _liblzma_archive)
set(_liblzma_url "${ENVY_LIBLZMA_URL}")
if(EXISTS "${_liblzma_archive}")
    file(TO_CMAKE_PATH "${_liblzma_archive}" _liblzma_archive_norm)
    set(_liblzma_url "file://${_liblzma_archive_norm}")
endif()

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(envy_liblzma
    URL ${_liblzma_url}
    URL_HASH SHA256=${ENVY_LIBLZMA_SHA256}
)
FetchContent_MakeAvailable(envy_liblzma)
FetchContent_GetProperties(envy_liblzma)

if(DEFINED envy_liblzma_BINARY_DIR)
    set(_envy_liblzma_primary_target "")
    if(TARGET liblzma::liblzma)
        set(_envy_liblzma_primary_target liblzma::liblzma)
    elseif(TARGET liblzma)
        set(_envy_liblzma_primary_target liblzma)
    elseif(TARGET lzma)
        set(_envy_liblzma_primary_target lzma)
    endif()

    if(_envy_liblzma_primary_target STREQUAL "")
        message(FATAL_ERROR "Failed to locate liblzma target after FetchContent_MakeAvailable")
    endif()

    unset(LIBLZMA_LIBRARY CACHE)
    unset(LIBLZMA_LIBRARIES CACHE)

    set(LIBLZMA_LIBRARY "${_envy_liblzma_primary_target}" CACHE STRING "" FORCE)
    if(DEFINED envy_liblzma_SOURCE_DIR)
        set(LIBLZMA_INCLUDE_DIR "${envy_liblzma_SOURCE_DIR}/src/liblzma/api" CACHE PATH "" FORCE)
        set(LIBLZMA_INCLUDE_DIRS "${LIBLZMA_INCLUDE_DIR}" CACHE STRING "" FORCE)
    endif()

    if(NOT TARGET LibLZMA::LibLZMA)
        add_library(LibLZMA::LibLZMA INTERFACE IMPORTED)
    endif()
    set(_envy_liblzma_include_paths "")
    if(DEFINED envy_liblzma_SOURCE_DIR)
        list(APPEND _envy_liblzma_include_paths "${envy_liblzma_SOURCE_DIR}/src/liblzma/api")
    endif()

    set_target_properties(LibLZMA::LibLZMA PROPERTIES
        INTERFACE_LINK_LIBRARIES ${_envy_liblzma_primary_target}
        INTERFACE_INCLUDE_DIRECTORIES "${_envy_liblzma_include_paths}")

    set(LIBLZMA_LIBRARIES "${LIBLZMA_LIBRARY}" CACHE STRING "" FORCE)
    set(LIBLZMA_VERSION_STRING "${ENVY_LIBLZMA_VERSION}" CACHE STRING "" FORCE)
    set(LIBLZMA_HAS_AUTO_DECODER 1 CACHE INTERNAL "" FORCE)
    set(LIBLZMA_HAS_EASY_ENCODER 1 CACHE INTERNAL "" FORCE)
    set(LIBLZMA_HAS_LZMA_PRESET 1 CACHE INTERNAL "" FORCE)
    set(LIBLZMA_FOUND TRUE CACHE BOOL "" FORCE)
    set(LibLZMA_FOUND TRUE CACHE BOOL "" FORCE)
endif()

unset(_envy_liblzma_include_paths)
unset(_envy_liblzma_primary_target)
unset(_liblzma_archive)
unset(_liblzma_archive_norm)
unset(_liblzma_url)

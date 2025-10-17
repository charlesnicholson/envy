cmake_path(APPEND ENVY_THIRDPARTY_CACHE_DIR "${ENVY_LIBZSTD_ARCHIVE}" OUTPUT_VARIABLE _libzstd_archive)
set(_libzstd_url "${ENVY_LIBZSTD_URL}")
if(EXISTS "${_libzstd_archive}")
    file(TO_CMAKE_PATH "${_libzstd_archive}" _libzstd_archive_norm)
    set(_libzstd_url "file://${_libzstd_archive_norm}")
endif()

set(_libzstd_extra_args)
set(_libzstd_cache_args)
if(MSVC AND CMAKE_ASM_COMPILER)
    list(APPEND _libzstd_cache_args
        "-DCMAKE_ASM_COMPILER:FILEPATH=${CMAKE_ASM_COMPILER}"
        "-DCMAKE_ASM_MASM_COMPILER:FILEPATH=${CMAKE_ASM_COMPILER}"
    )
endif()
if(POLICY CMP0194)
    list(APPEND _libzstd_cache_args "-DCMAKE_POLICY_DEFAULT_CMP0194:STRING=NEW")
endif()
if(_libzstd_cache_args)
    list(PREPEND _libzstd_cache_args CMAKE_CACHE_ARGS)
    list(APPEND _libzstd_extra_args ${_libzstd_cache_args})
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
    ${_libzstd_extra_args}
)
FetchContent_GetProperties(envy_libzstd)
set(_libzstd_was_populated ${envy_libzstd_POPULATED})
if(NOT envy_libzstd_POPULATED)
    if(POLICY CMP0169)
        cmake_policy(PUSH)
        cmake_policy(SET CMP0169 OLD)
    endif()
    FetchContent_Populate(envy_libzstd)
    if(POLICY CMP0169)
        cmake_policy(POP)
    endif()
    set(envy_libzstd_POPULATED TRUE)
endif()
FetchContent_GetProperties(envy_libzstd)

if(MSVC)
    set(_libzstd_root_cmakelists "${envy_libzstd_SOURCE_DIR}/build/cmake/CMakeLists.txt")
    if(EXISTS "${_libzstd_root_cmakelists}")
        message(STATUS "[envy] Patching zstd root project for MASM")
        file(READ "${_libzstd_root_cmakelists}" _libzstd_root_content)
        set(_libzstd_root_patched "${_libzstd_root_content}")

    string(REPLACE "LANGUAGES C   # Main library is in C\n            ASM # And ASM" "LANGUAGES C   # Main library is in C\n            ASM_MASM # MASM on Windows" _libzstd_root_patched "${_libzstd_root_patched}")
        string(REPLACE "\nenable_language(ASM_MASM)" "" _libzstd_root_patched "${_libzstd_root_patched}")
        string(REPLACE "enable_language(ASM_MASM)\n" "" _libzstd_root_patched "${_libzstd_root_patched}")

        string(FIND "${_libzstd_root_patched}" "cmake_policy(SET CMP0194 NEW)" _libzstd_root_cmp0194)
        if(_libzstd_root_cmp0194 EQUAL -1)
            string(REPLACE "project(zstd" "cmake_policy(SET CMP0194 NEW)\nproject(zstd" _libzstd_root_patched "${_libzstd_root_patched}")
        endif()
        unset(_libzstd_root_cmp0194)

        if(NOT _libzstd_root_content STREQUAL _libzstd_root_patched)
            file(WRITE "${_libzstd_root_cmakelists}" "${_libzstd_root_patched}")
        endif()
        unset(_libzstd_root_content)
        unset(_libzstd_root_patched)
    endif()

    set(_libzstd_lib_cmakelists "${envy_libzstd_SOURCE_DIR}/build/cmake/lib/CMakeLists.txt")
    if(EXISTS "${_libzstd_lib_cmakelists}")
        message(STATUS "[envy] Patching zstd lib project for MASM")
        file(READ "${_libzstd_lib_cmakelists}" _libzstd_lib_content)
        set(_libzstd_lib_patched "${_libzstd_lib_content}")

        string(REPLACE "project(libzstd C ASM)" "project(libzstd C ASM_MASM)" _libzstd_lib_patched "${_libzstd_lib_patched}")
        string(REPLACE "enable_language(ASM_MASM)\n" "" _libzstd_lib_patched "${_libzstd_lib_patched}")

        if(NOT _libzstd_lib_content STREQUAL _libzstd_lib_patched)
            file(WRITE "${_libzstd_lib_cmakelists}" "${_libzstd_lib_patched}")
        endif()
        unset(_libzstd_lib_content)
        unset(_libzstd_lib_patched)
    endif()
    unset(_libzstd_lib_cmakelists)
    unset(_libzstd_root_cmakelists)
endif()

if(NOT _libzstd_was_populated OR NOT TARGET libzstd_static)
    add_subdirectory("${envy_libzstd_SOURCE_DIR}/build/cmake" "${envy_libzstd_BINARY_DIR}")
endif()

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
unset(_libzstd_root_cmakelists)
unset(_libzstd_lib_cmakelists)
unset(_libzstd_cache_args)
unset(_libzstd_extra_args)
unset(_libzstd_archive)
unset(_libzstd_archive_norm)
unset(_libzstd_url)

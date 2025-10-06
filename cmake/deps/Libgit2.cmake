set(USE_HTTPS SecureTransport CACHE STRING "" FORCE)
set(USE_SSH ON CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_CLI OFF CACHE BOOL "" FORCE)
set(CMAKE_DISABLE_FIND_PACKAGE_PkgConfig ON)
cmake_path(APPEND CODEX_THIRDPARTY_CACHE_DIR "${CODEX_LIBGIT2_ARCHIVE}" OUTPUT_VARIABLE _libgit2_archive)
set(_libgit2_url "${CODEX_LIBGIT2_URL}")
if(EXISTS "${_libgit2_archive}")
    file(TO_CMAKE_PATH "${_libgit2_archive}" _libgit2_archive_norm)
    set(_libgit2_url "file://${_libgit2_archive_norm}")
endif()
FetchContent_Declare(libgit2
    URL ${_libgit2_url}
    URL_HASH SHA256=${CODEX_LIBGIT2_SHA256}
)
FetchContent_GetProperties(libgit2)
if(NOT libgit2_POPULATED)
    FetchContent_Populate(libgit2)
    FetchContent_GetProperties(libgit2)
endif()

if(DEFINED libgit2_SOURCE_DIR AND DEFINED libgit2_BINARY_DIR AND
        DEFINED libssh2_SOURCE_DIR AND DEFINED libssh2_BINARY_DIR)
    codex_patch_libgit2_select("${libgit2_SOURCE_DIR}" "${libgit2_BINARY_DIR}" "${libssh2_SOURCE_DIR}" "${libssh2_BINARY_DIR}")
endif()

add_subdirectory(${libgit2_SOURCE_DIR} ${libgit2_BINARY_DIR})
if(TARGET libgit2package)
    add_library(codex::libgit2 ALIAS libgit2package)
    target_include_directories(libgit2package INTERFACE
        "$<BUILD_INTERFACE:${libgit2_SOURCE_DIR}/include>"
        "$<BUILD_INTERFACE:${libgit2_BINARY_DIR}/include>")
elseif(TARGET git2)
    add_library(codex::libgit2 ALIAS git2)
elseif(TARGET libgit2)
    add_library(codex::libgit2 ALIAS libgit2)
else()
    message(FATAL_ERROR "libgit2 target was not created by FetchContent")
endif()

foreach(_codex_git_target IN ITEMS libgit2package git2 libgit2)
    if(TARGET ${_codex_git_target})
        get_target_property(_codex_git_iface ${_codex_git_target} INTERFACE_LINK_LIBRARIES)
        if(_codex_git_iface)
            list(REMOVE_ITEM _codex_git_iface libssh2::libssh2 "${LIBSSH2_LIBRARY}")
            set_target_properties(${_codex_git_target} PROPERTIES
                INTERFACE_LINK_LIBRARIES "${_codex_git_iface}")
        endif()
    endif()
endforeach()
unset(_codex_git_target)
unset(_codex_git_iface)

set(_codex_libgit2_warning_silencers
    $<$<COMPILE_LANG_AND_ID:C,AppleClang>:-Wno-declaration-after-statement>
    $<$<COMPILE_LANG_AND_ID:C,AppleClang>:-Wno-unused-but-set-parameter>
    $<$<COMPILE_LANG_AND_ID:C,AppleClang>:-Wno-single-bit-bitfield-constant-conversion>
    $<$<COMPILE_LANG_AND_ID:C,AppleClang>:-Wno-array-parameter>
)
foreach(_libgit2_target IN ITEMS libgit2 libgit2package util ntlmclient http-parser xdiff)
    if(TARGET ${_libgit2_target})
        target_compile_options(${_libgit2_target} PRIVATE ${_codex_libgit2_warning_silencers})
    endif()
endforeach()

unset(_libgit2_archive)
unset(_libgit2_archive_norm)
unset(_libgit2_url)
unset(_libgit2_target)
unset(_codex_libgit2_warning_silencers)
unset(CMAKE_DISABLE_FIND_PACKAGE_PkgConfig)

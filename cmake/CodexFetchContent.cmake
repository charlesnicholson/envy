# Helper utilities for fetching third-party dependencies while
# preserving the repository's cache layout and offline workflows.

include(FetchContent)

function(codex_fetchcontent_acquire name human_name)
    set(options MAKE_AVAILABLE)
    set(oneValueArgs OUT_WAS_POPULATED)
    cmake_parse_arguments(_codex "${options}" "${oneValueArgs}" "" ${ARGN})

    string(TOLOWER "${name}" _codex_lower)
    cmake_path(APPEND FETCHCONTENT_BASE_DIR "${_codex_lower}-src" OUTPUT_VARIABLE _codex_source_dir)
    cmake_path(APPEND CMAKE_BINARY_DIR "_deps" "${_codex_lower}-build" OUTPUT_VARIABLE _codex_binary_dir)

    set(_codex_prev_defined FALSE)
    if(DEFINED FETCHCONTENT_FULLY_DISCONNECTED)
        set(_codex_prev_defined TRUE)
        set(_codex_prev_value "${FETCHCONTENT_FULLY_DISCONNECTED}")
    endif()

    set(_codex_have_cached_sources FALSE)
    if(EXISTS "${_codex_source_dir}")
        set(_codex_have_cached_sources TRUE)
        set(FETCHCONTENT_FULLY_DISCONNECTED ON)
    endif()

    FetchContent_GetProperties(${name})
    set(_codex_was_populated FALSE)
    if(DEFINED ${name}_POPULATED)
        set(_codex_was_populated "${${name}_POPULATED}")
    endif()

    if(_codex_have_cached_sources AND NOT _codex_was_populated)
        message(STATUS "[codex] Reusing cached ${human_name} sources at ${_codex_source_dir}")
    elseif(NOT _codex_was_populated)
        message(STATUS "[codex] No cached ${human_name} sources at ${_codex_source_dir}; fetching")
    endif()

    FetchContent_Populate(${name})
    FetchContent_GetProperties(${name})

    if(_codex_prev_defined)
        set(FETCHCONTENT_FULLY_DISCONNECTED "${_codex_prev_value}")
    else()
        unset(FETCHCONTENT_FULLY_DISCONNECTED)
    endif()

    if(_codex_MAKE_AVAILABLE)
        FetchContent_MakeAvailable(${name})
    endif()

    set(${name}_SOURCE_DIR "${_codex_source_dir}" PARENT_SCOPE)
    set(${name}_BINARY_DIR "${_codex_binary_dir}" PARENT_SCOPE)
    if(DEFINED ${name}_POPULATED)
        set(${name}_POPULATED "${${name}_POPULATED}" PARENT_SCOPE)
    endif()

    if(_codex_OUT_WAS_POPULATED)
        set(${_codex_OUT_WAS_POPULATED} ${_codex_was_populated} PARENT_SCOPE)
    endif()

    unset(_codex_lower)
    unset(_codex_source_dir)
    unset(_codex_binary_dir)
    unset(_codex_prev_defined)
    unset(_codex_prev_value)
    unset(_codex_have_cached_sources)
    unset(_codex_was_populated)
    unset(_codex_MAKE_AVAILABLE)
    unset(_codex_OUT_WAS_POPULATED)
endfunction()

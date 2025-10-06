# Helper utilities for fetching third-party dependencies while
# preserving the repository's cache layout and offline workflows.

include(FetchContent)

function(envy_fetchcontent_acquire name human_name)
    set(options MAKE_AVAILABLE)
    set(oneValueArgs OUT_WAS_POPULATED)
    cmake_parse_arguments(_envy "${options}" "${oneValueArgs}" "" ${ARGN})

    string(TOLOWER "${name}" _envy_lower)
    cmake_path(APPEND FETCHCONTENT_BASE_DIR "${_envy_lower}-src" OUTPUT_VARIABLE _envy_source_dir)
    cmake_path(APPEND CMAKE_BINARY_DIR "_deps" "${_envy_lower}-build" OUTPUT_VARIABLE _envy_binary_dir)

    set(_envy_prev_defined FALSE)
    if(DEFINED FETCHCONTENT_FULLY_DISCONNECTED)
        set(_envy_prev_defined TRUE)
        set(_envy_prev_value "${FETCHCONTENT_FULLY_DISCONNECTED}")
    endif()

    set(_envy_have_cached_sources FALSE)
    if(EXISTS "${_envy_source_dir}")
        set(_envy_have_cached_sources TRUE)
        set(FETCHCONTENT_FULLY_DISCONNECTED ON)
    endif()

    FetchContent_GetProperties(${name})
    set(_envy_was_populated FALSE)
    if(DEFINED ${name}_POPULATED)
        set(_envy_was_populated "${${name}_POPULATED}")
    endif()

    if(_envy_have_cached_sources AND NOT _envy_was_populated)
        message(STATUS "[envy] Reusing cached ${human_name} sources at ${_envy_source_dir}")
    elseif(NOT _envy_was_populated)
        message(STATUS "[envy] No cached ${human_name} sources at ${_envy_source_dir}; fetching")
    endif()

    if(NOT DEFINED ${name}_SOURCE_DIR)
        string(TOLOWER "${name}" _envy_lower_auto)
        cmake_path(APPEND FETCHCONTENT_BASE_DIR "${_envy_lower_auto}-src" OUTPUT_VARIABLE ${name}_SOURCE_DIR)
        cmake_path(APPEND CMAKE_BINARY_DIR "_deps" "${_envy_lower_auto}-build" OUTPUT_VARIABLE ${name}_BINARY_DIR)
        unset(_envy_lower_auto)
    endif()

    FetchContent_Populate(${name}
        SOURCE_DIR "${${name}_SOURCE_DIR}"
        BINARY_DIR "${${name}_BINARY_DIR}"
    )
    FetchContent_GetProperties(${name})

    if(_envy_prev_defined)
        set(FETCHCONTENT_FULLY_DISCONNECTED "${_envy_prev_value}")
    else()
        unset(FETCHCONTENT_FULLY_DISCONNECTED)
    endif()

    if(_envy_MAKE_AVAILABLE)
        FetchContent_MakeAvailable(${name})
    endif()

    set(${name}_SOURCE_DIR "${_envy_source_dir}" PARENT_SCOPE)
    set(${name}_BINARY_DIR "${_envy_binary_dir}" PARENT_SCOPE)
    if(DEFINED ${name}_POPULATED)
        set(${name}_POPULATED "${${name}_POPULATED}" PARENT_SCOPE)
    endif()

    if(_envy_OUT_WAS_POPULATED)
        set(${_envy_OUT_WAS_POPULATED} ${_envy_was_populated} PARENT_SCOPE)
    endif()

    unset(_envy_lower)
    unset(_envy_source_dir)
    unset(_envy_binary_dir)
    unset(_envy_prev_defined)
    unset(_envy_prev_value)
    unset(_envy_have_cached_sources)
    unset(_envy_was_populated)
    unset(_envy_MAKE_AVAILABLE)
    unset(_envy_OUT_WAS_POPULATED)
endfunction()

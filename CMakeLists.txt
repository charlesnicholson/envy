cmake_minimum_required(VERSION 4.1.2)

project(envy
    VERSION 0.1.0
    DESCRIPTION "Static test driver bundling libgit2, libcurl (mbedTLS), libssh2, mbedTLS, Lua, oneTBB, libarchive, and BLAKE3"
    LANGUAGES C CXX)

list(PREPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake/modules")

if(POLICY CMP0069)
    cmake_policy(SET CMP0069 NEW)
    set(CMAKE_POLICY_DEFAULT_CMP0069 NEW)
endif()

if(POLICY CMP0194)
    cmake_policy(SET CMP0194 NEW)
    set(CMAKE_POLICY_DEFAULT_CMP0194 NEW)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
    enable_language(ASM_MASM)
    if(NOT CMAKE_ASM_MASM_COMPILER)
        message(FATAL_ERROR "MASM assembler was not detected")
    endif()
    set(CMAKE_ASM_COMPILER "${CMAKE_ASM_MASM_COMPILER}" CACHE FILEPATH "" FORCE)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded" CACHE STRING "" FORCE)
    set(_envy_msvc_flag_vars
        CMAKE_C_FLAGS
        CMAKE_C_FLAGS_DEBUG
        CMAKE_C_FLAGS_RELEASE
        CMAKE_C_FLAGS_RELWITHDEBINFO
        CMAKE_C_FLAGS_MINSIZEREL
        CMAKE_CXX_FLAGS
        CMAKE_CXX_FLAGS_DEBUG
        CMAKE_CXX_FLAGS_RELEASE
        CMAKE_CXX_FLAGS_RELWITHDEBINFO
        CMAKE_CXX_FLAGS_MINSIZEREL
    )
    foreach(_envy_flag_var IN LISTS _envy_msvc_flag_vars)
        if(DEFINED ${_envy_flag_var})
            string(REGEX REPLACE "/M[TD]d?" "" _envy_sanitized_flags "${${_envy_flag_var}}")
            string(STRIP "${_envy_sanitized_flags}" _envy_sanitized_flags)
            set(${_envy_flag_var} "${_envy_sanitized_flags}" CACHE STRING "" FORCE)
        endif()
    endforeach()
    unset(_envy_msvc_flag_vars)
    unset(_envy_flag_var)
    unset(_envy_sanitized_flags)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_compile_definitions(_POSIX_C_SOURCE=200809L)
endif()

# Favor size-optimized release binaries by normalizing optimization flags
set(_envy_size_opt "-Os")
if(MSVC)
    set(_envy_size_opt "/Os")
endif()

foreach(_envy_lang IN ITEMS C CXX)
    set(_envy_flags "${CMAKE_${_envy_lang}_FLAGS_RELEASE}")
    string(REGEX REPLACE "-O[0-9A-Za-z]*" "" _envy_flags "${_envy_flags}")
    string(REGEX REPLACE "/O[0-9A-Za-z]*" "" _envy_flags "${_envy_flags}")
    string(STRIP "${_envy_flags}" _envy_flags)
    if(_envy_flags STREQUAL "")
        set(_envy_flags "${_envy_size_opt}")
    else()
        string(APPEND _envy_flags " ${_envy_size_opt}")
    endif()
    set(CMAKE_${_envy_lang}_FLAGS_RELEASE "${_envy_flags}" CACHE STRING "Release flags" FORCE)
endforeach()
unset(_envy_size_opt)
unset(_envy_flags)
unset(_envy_lang)

option(ENABLE_LTO "Enable interprocedural optimization" ON)
if(ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_supported OUTPUT ipo_output)
    if(ipo_supported)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL ON)
        # Explicitly add LTO flags to linker for all compilers
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
            foreach(_type RELEASE RELWITHDEBINFO MINSIZEREL)
                set(CMAKE_EXE_LINKER_FLAGS_${_type} "${CMAKE_EXE_LINKER_FLAGS_${_type}} -flto=thin" CACHE STRING "${_type} exe linker flags" FORCE)
                set(CMAKE_SHARED_LINKER_FLAGS_${_type} "${CMAKE_SHARED_LINKER_FLAGS_${_type}} -flto=thin" CACHE STRING "${_type} shared linker flags" FORCE)
            endforeach()
        elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            foreach(_type RELEASE RELWITHDEBINFO MINSIZEREL)
                set(CMAKE_EXE_LINKER_FLAGS_${_type} "${CMAKE_EXE_LINKER_FLAGS_${_type}} -flto -Wl,-z,noexecstack" CACHE STRING "${_type} exe linker flags" FORCE)
                set(CMAKE_SHARED_LINKER_FLAGS_${_type} "${CMAKE_SHARED_LINKER_FLAGS_${_type}} -flto -Wl,-z,noexecstack" CACHE STRING "${_type} shared linker flags" FORCE)
            endforeach()
        elseif(MSVC)
            foreach(_type RELEASE RELWITHDEBINFO MINSIZEREL)
                set(CMAKE_EXE_LINKER_FLAGS_${_type} "${CMAKE_EXE_LINKER_FLAGS_${_type}} /LTCG" CACHE STRING "${_type} exe linker flags" FORCE)
                set(CMAKE_SHARED_LINKER_FLAGS_${_type} "${CMAKE_SHARED_LINKER_FLAGS_${_type}} /LTCG" CACHE STRING "${_type} shared linker flags" FORCE)
            endforeach()
        endif()
    else()
        message(WARNING "IPO/LTO not supported: ${ipo_output}")
    endif()
endif()

include(cmake/Dependencies.cmake)

add_executable(envy)

target_sources(envy
    PRIVATE
        src/main.cpp
        src/cli.cpp
        src/cmd.cpp
        src/cmd_lua.cpp
        src/cmd_playground.cpp
        src/cmd_version.cpp
)

target_compile_features(envy PRIVATE cxx_std_20)
target_compile_options(envy PRIVATE
    $<$<COMPILE_LANG_AND_ID:CXX,AppleClang,Clang,GNU>:-ffunction-sections>
    $<$<COMPILE_LANG_AND_ID:CXX,AppleClang,Clang,GNU>:-fdata-sections>
    $<$<COMPILE_LANG_AND_ID:C,AppleClang,Clang,GNU>:-ffunction-sections>
    $<$<COMPILE_LANG_AND_ID:C,AppleClang,Clang,GNU>:-fdata-sections>
)
target_link_options(envy PRIVATE
    $<$<PLATFORM_ID:Darwin>:-Wl,-dead_strip>
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(CMAKE_C_LINK_GROUP_USING_RESCAN_SUPPORTED TRUE CACHE BOOL "" FORCE)
    set(CMAKE_C_LINK_GROUP_USING_RESCAN
        "LINKER:--start-group"
        "LINKER:--end-group"
        CACHE STRING "" FORCE
    )
    set(CMAKE_CXX_LINK_GROUP_USING_RESCAN_SUPPORTED TRUE CACHE BOOL "" FORCE)
    set(CMAKE_CXX_LINK_GROUP_USING_RESCAN
        "LINKER:--start-group"
        "LINKER:--end-group"
        CACHE STRING "" FORCE
    )
endif()

target_link_libraries(envy
    PRIVATE
        envy::thirdparty
)

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin" OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if(NOT CMAKE_STRIP)
        find_program(CMAKE_STRIP NAMES strip)
    endif()
    if(CMAKE_STRIP)
        set(_envy_strip_args)
        if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
            set(_envy_strip_args -x)
        elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            set(_envy_strip_args --strip-all)
        endif()
        add_custom_command(
            TARGET envy POST_BUILD
            COMMAND "${CMAKE_STRIP}" ${_envy_strip_args} "$<TARGET_FILE:envy>"
            COMMENT "Stripping envy executable"
            VERBATIM
        )
        unset(_envy_strip_args)
    endif()
endif()

if(CMAKE_CXX_LINK_GROUP_USING_RESCAN_SUPPORTED)
    set(_envy_rescan_targets
        envy::libgit2
        libssh2::libssh2
        CURL::libcurl
        MbedTLS::mbedtls
        MbedTLS::mbedx509
        MbedTLS::mbedcrypto
        ZLIB::ZLIB
    )
    list(APPEND _envy_rescan_targets ${PLATFORM_NETWORK_LIBS})
    set(_envy_rescan_expanded_targets)
    foreach(_envy_candidate IN LISTS _envy_rescan_targets)
        if(TARGET ${_envy_candidate})
            get_target_property(_envy_candidate_type ${_envy_candidate} TYPE)
            if(_envy_candidate_type STREQUAL "INTERFACE_LIBRARY")
                unset(_envy_candidate_type)
                continue()
            else()
                list(APPEND _envy_rescan_expanded_targets ${_envy_candidate})
            endif()
            unset(_envy_candidate_type)
        else()
            list(APPEND _envy_rescan_expanded_targets ${_envy_candidate})
        endif()
    endforeach()
    list(REMOVE_DUPLICATES _envy_rescan_expanded_targets)
    list(JOIN _envy_rescan_expanded_targets "," _envy_link_group)
    target_link_libraries(envy
        PRIVATE
            "$<LINK_GROUP:RESCAN,${_envy_link_group}>"
    )
    unset(_envy_link_group)
    unset(_envy_rescan_targets)
    unset(_envy_rescan_expanded_targets)
endif()

# Unit tests ---------------------------------------------------------------
add_executable(envy_unit_tests)

target_sources(envy_unit_tests
    PRIVATE
        src/unit_test_main.cpp
        src/cli.cpp
        src/cmd.cpp
        src/cmd_lua.cpp
        src/cmd_version.cpp
        src/cmd_playground.cpp
        src/cli_tests.cpp
        src/cmd_tests.cpp
        src/cmd_lua_tests.cpp
        src/cmd_version_tests.cpp
)

target_compile_features(envy_unit_tests PRIVATE cxx_std_20)
target_link_libraries(envy_unit_tests
    PRIVATE
        doctest::doctest
        envy::thirdparty
)

# Run tests and create timestamp on success
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/envy_unit_tests.timestamp"
    COMMAND envy_unit_tests
    COMMAND ${CMAKE_COMMAND} -E touch "${CMAKE_CURRENT_BINARY_DIR}/envy_unit_tests.timestamp"
    DEPENDS envy_unit_tests
    COMMENT "Running unit tests"
    VERBATIM
)

add_custom_target(run_unit_tests ALL
    DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/envy_unit_tests.timestamp"
)

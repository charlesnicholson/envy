cmake_minimum_required(VERSION 4.1.2)

set(ENVY_VERSION "0.0.0")

project(envy
    VERSION ${ENVY_VERSION}
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

find_package(Python3 3.13 EXACT REQUIRED COMPONENTS Interpreter)

if(MSVC)
    enable_language(ASM_MASM)
    if(NOT CMAKE_ASM_MASM_COMPILER)
        message(FATAL_ERROR "MASM assembler was not detected")
    endif()
    set(CMAKE_ASM_COMPILER "${CMAKE_ASM_MASM_COMPILER}" CACHE FILEPATH "" FORCE)
    set(CMAKE_ASM_MASM_FLAGS "/nologo" CACHE STRING "" FORCE)
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
                if(NOT "${CMAKE_EXE_LINKER_FLAGS_${_type}}" MATCHES "-flto")
                    set(CMAKE_EXE_LINKER_FLAGS_${_type} "${CMAKE_EXE_LINKER_FLAGS_${_type}} -flto=thin" CACHE STRING "${_type} exe linker flags" FORCE)
                endif()
                if(NOT "${CMAKE_SHARED_LINKER_FLAGS_${_type}}" MATCHES "-flto")
                    set(CMAKE_SHARED_LINKER_FLAGS_${_type} "${CMAKE_SHARED_LINKER_FLAGS_${_type}} -flto=thin" CACHE STRING "${_type} shared linker flags" FORCE)
                endif()
            endforeach()
        elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            foreach(_type RELEASE RELWITHDEBINFO MINSIZEREL)
                if(NOT "${CMAKE_EXE_LINKER_FLAGS_${_type}}" MATCHES "-flto")
                    set(CMAKE_EXE_LINKER_FLAGS_${_type} "${CMAKE_EXE_LINKER_FLAGS_${_type}} -flto" CACHE STRING "${_type} exe linker flags" FORCE)
                endif()
                if(NOT "${CMAKE_SHARED_LINKER_FLAGS_${_type}}" MATCHES "-flto")
                    set(CMAKE_SHARED_LINKER_FLAGS_${_type} "${CMAKE_SHARED_LINKER_FLAGS_${_type}} -flto" CACHE STRING "${_type} shared linker flags" FORCE)
                endif()
            endforeach()
        elseif(MSVC)
            foreach(_type RELEASE RELWITHDEBINFO MINSIZEREL)
                if(NOT "${CMAKE_EXE_LINKER_FLAGS_${_type}}" MATCHES "/LTCG")
                    set(CMAKE_EXE_LINKER_FLAGS_${_type} "${CMAKE_EXE_LINKER_FLAGS_${_type}} /LTCG" CACHE STRING "${_type} exe linker flags" FORCE)
                endif()
                if(NOT "${CMAKE_SHARED_LINKER_FLAGS_${_type}}" MATCHES "/LTCG")
                    set(CMAKE_SHARED_LINKER_FLAGS_${_type} "${CMAKE_SHARED_LINKER_FLAGS_${_type}} /LTCG" CACHE STRING "${_type} shared linker flags" FORCE)
                endif()
            endforeach()
        endif()
    else()
        message(WARNING "IPO/LTO not supported: ${ipo_output}")
    endif()
endif()

# Enable AddressSanitizer globally on MSVC when ENVY_ENABLE_ASAN is set
if(MSVC AND ENVY_ENABLE_ASAN)
    add_compile_options($<$<COMPILE_LANGUAGE:C>:/fsanitize=address>)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/fsanitize=address>)
    add_link_options(/INCREMENTAL:NO)
    message(STATUS "MSVC: AddressSanitizer enabled globally for C/C++ targets")
endif()

include(cmake/Dependencies.cmake)

# Common source files shared by all envy executables
set(ENVY_COMMON_SOURCES
    src/main.cpp
    src/cache.cpp
    src/cli.cpp
    src/cmd.cpp
    src/cmds/cmd_asset.cpp
    src/cmds/cmd_common.cpp
    src/cmds/cmd_extract.cpp
    src/cmds/cmd_fetch.cpp
    src/cmds/cmd_hash.cpp
    src/cmds/cmd_lua.cpp
    src/cmds/cmd_product.cpp
    src/cmds/cmd_sync.cpp
    src/cmds/cmd_version.cpp
    src/engine.cpp
    src/recipe_phase.cpp
    src/recipe_key.cpp
    src/lua_ctx/lua_ctx_bindings.cpp
    src/lua_ctx/lua_ctx_asset.cpp
    src/lua_ctx/lua_ctx_commit_fetch.cpp
    src/lua_ctx/lua_ctx_copy.cpp
    src/lua_ctx/lua_ctx_extract.cpp
    src/lua_ctx/lua_ctx_extract_all.cpp
    src/lua_ctx/lua_ctx_fetch.cpp
    src/lua_ctx/lua_ctx_ls.cpp
    src/lua_ctx/lua_ctx_move.cpp
    src/lua_ctx/lua_ctx_run.cpp
    src/lua_ctx/lua_ctx_product.cpp
    src/product_util.cpp
    src/phases/phase_build.cpp
    src/phases/phase_check.cpp
    src/phases/phase_completion.cpp
    src/phases/phase_deploy.cpp
    src/phases/phase_fetch.cpp
    src/phases/phase_install.cpp
    src/phases/phase_recipe_fetch.cpp
    src/phases/phase_stage.cpp
    src/extract.cpp
    src/aws_util.cpp
    src/blake3_util.cpp
    src/libgit2_util.cpp
    src/sol_util.cpp
    src/lua_shell.cpp
    src/lua_envy.cpp
    src/lua_error_formatter.cpp
    src/manifest.cpp
    src/recipe_spec.cpp
    $<$<PLATFORM_ID:Windows>:src/sha256_win.cpp>
    $<$<NOT:$<PLATFORM_ID:Windows>>:src/sha256_mbedtls.cpp>
    src/fetch.cpp
    src/libcurl_util.cpp
    src/platform.cpp
    src/shell.cpp
    $<$<PLATFORM_ID:Windows>:src/shell_win32.cpp>
    $<$<NOT:$<PLATFORM_ID:Windows>>:src/shell_posix.cpp>
    src/trace.cpp
    src/tui.cpp
    src/uri.cpp
    src/util.cpp
)

# Functional test-specific sources
set(ENVY_FUNCTIONAL_TEST_SOURCES
    src/cmds/cmd_cache_functional_test.cpp
    src/cmds/cmd_engine_functional_test.cpp
    src/recipe_phase.cpp
    src/test_support.cpp
)

# Common compile options
set(ENVY_COMMON_COMPILE_OPTIONS
    $<$<COMPILE_LANG_AND_ID:CXX,AppleClang,Clang,GNU>:-ffunction-sections>
    $<$<COMPILE_LANG_AND_ID:CXX,AppleClang,Clang,GNU>:-fdata-sections>
    $<$<COMPILE_LANG_AND_ID:C,AppleClang,Clang,GNU>:-ffunction-sections>
    $<$<COMPILE_LANG_AND_ID:C,AppleClang,Clang,GNU>:-fdata-sections>
    $<$<COMPILE_LANG_AND_ID:CXX,GNU>:-Wno-format-zero-length>
    $<$<COMPILE_LANG_AND_ID:C,GNU>:-Wno-format-zero-length>
)

# Function to configure an envy executable
function(configure_envy_executable TARGET_NAME SANITIZERS)
    target_compile_features(${TARGET_NAME} PRIVATE cxx_std_20)
    target_include_directories(${TARGET_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(${TARGET_NAME} PRIVATE ${ENVY_COMMON_COMPILE_OPTIONS})
    target_link_options(${TARGET_NAME} PRIVATE
        $<$<PLATFORM_ID:Darwin>:-Wl,-dead_strip>
    )
    target_link_libraries(${TARGET_NAME} PRIVATE envy::thirdparty)

    # Add sanitizer flags if specified (only for Clang/GCC; MSVC is handled globally)
    if(SANITIZERS AND NOT MSVC)
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            target_compile_options(${TARGET_NAME} PRIVATE
                $<$<OR:$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:GNU>>:-fsanitize=${SANITIZERS} -fno-omit-frame-pointer -g>
                $<$<OR:$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:Clang>>:-fsanitize-blacklist=${PROJECT_SOURCE_DIR}/sanitizer_blacklist.txt>
            )
            target_link_options(${TARGET_NAME} PRIVATE
                $<$<OR:$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:GNU>>:-fsanitize=${SANITIZERS}>
            )
            # Suppress TSAN warnings about atomic_thread_fence in third-party code
            if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND SANITIZERS MATCHES "thread")
                target_link_options(${TARGET_NAME} PRIVATE
                    -Wno-tsan
                )
            endif()
        endif()
    endif()

    # Add noexecstack for all GCC builds (security hardening)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_link_options(${TARGET_NAME} PRIVATE
            $<$<CONFIG:Release,RelWithDebInfo,MinSizeRel>:-Wl,-z,noexecstack>
        )
    endif()
endfunction()

# Production executable -------------------------------------------------------
add_executable(envy)
target_sources(envy PRIVATE ${ENVY_COMMON_SOURCES})
target_compile_definitions(envy PRIVATE ENVY_VERSION_STR=\"${PROJECT_VERSION}\")
configure_envy_executable(envy "")

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

if((CMAKE_SYSTEM_NAME STREQUAL "Darwin" OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
        AND CMAKE_BUILD_TYPE MATCHES "^(Release|RelWithDebInfo)$")
    if(NOT CMAKE_STRIP)
        find_program(CMAKE_STRIP NAMES strip)
    endif()
    if(CMAKE_STRIP)
        add_custom_command(
            TARGET envy POST_BUILD
            COMMAND "${CMAKE_STRIP}" "$<TARGET_FILE:envy>"
            COMMENT "Stripping envy executable"
            VERBATIM
        )
    endif()
endif()

# Functional tester executables with different sanitizers -------------------
if(MSVC)
    # MSVC only supports AddressSanitizer (not UBSan/TSan)
    # Build single functional tester without sanitizers (third-party libs not built with ASan)
    add_executable(envy_functional_tester)
    target_sources(envy_functional_tester PRIVATE ${ENVY_COMMON_SOURCES} ${ENVY_FUNCTIONAL_TEST_SOURCES})
    target_compile_definitions(envy_functional_tester PRIVATE
        ENVY_VERSION_STR=\"${PROJECT_VERSION}\"
        ENVY_FUNCTIONAL_TESTER=1
    )
    configure_envy_executable(envy_functional_tester "")
else()
    # Clang/GCC: Build two sanitizer variants
    # ASan+UBSan variant
    add_executable(envy_functional_tester_asan_ubsan)
    target_sources(envy_functional_tester_asan_ubsan PRIVATE ${ENVY_COMMON_SOURCES} ${ENVY_FUNCTIONAL_TEST_SOURCES})
    target_compile_definitions(envy_functional_tester_asan_ubsan PRIVATE
        ENVY_VERSION_STR=\"${PROJECT_VERSION}\"
        ENVY_FUNCTIONAL_TESTER=1
    )
    # GCC: Disable ASan to avoid third-party false positives (mbedTLS, AWS SDK)
    # Clang: Use -fsanitize-blacklist (configured in configure_envy_executable)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        configure_envy_executable(envy_functional_tester_asan_ubsan "undefined")
    else()
        configure_envy_executable(envy_functional_tester_asan_ubsan "address,undefined")
    endif()

    # TSan+UBSan variant
    add_executable(envy_functional_tester_tsan_ubsan)
    target_sources(envy_functional_tester_tsan_ubsan PRIVATE ${ENVY_COMMON_SOURCES} ${ENVY_FUNCTIONAL_TEST_SOURCES})
    target_compile_definitions(envy_functional_tester_tsan_ubsan PRIVATE
        ENVY_VERSION_STR=\"${PROJECT_VERSION}\"
        ENVY_FUNCTIONAL_TESTER=1
    )
    configure_envy_executable(envy_functional_tester_tsan_ubsan "thread,undefined")

    # Legacy name alias for backward compatibility
    add_executable(envy_functional_tester ALIAS envy_functional_tester_asan_ubsan)
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
    list(JOIN _envy_rescan_expanded_targets "," _envy_link_group_entries)
    set(_envy_rescan_link_group "$<LINK_GROUP:RESCAN,${_envy_link_group_entries}>")
    unset(_envy_link_group_entries)
    unset(_envy_rescan_targets)
    unset(_envy_rescan_expanded_targets)
endif()

# Unit tests ---------------------------------------------------------------
# Common sources minus main.cpp for unit tests (which have unit_test_main.cpp)
set(ENVY_UNIT_TEST_COMMON_SOURCES
    src/cache.cpp
    src/cli.cpp
    src/cmd.cpp
    src/cmds/cmd_asset.cpp
    src/cmds/cmd_common.cpp
    src/cmds/cmd_extract.cpp
    src/cmds/cmd_fetch.cpp
    src/cmds/cmd_hash.cpp
    src/cmds/cmd_lua.cpp
    src/cmds/cmd_product.cpp
    src/cmds/cmd_sync.cpp
    src/cmds/cmd_version.cpp
    src/engine.cpp
    src/recipe_key.cpp
    src/lua_ctx/lua_ctx_bindings.cpp
    src/lua_ctx/lua_ctx_asset.cpp
    src/lua_ctx/lua_ctx_commit_fetch.cpp
    src/lua_ctx/lua_ctx_copy.cpp
    src/lua_ctx/lua_ctx_extract.cpp
    src/lua_ctx/lua_ctx_extract_all.cpp
    src/lua_ctx/lua_ctx_fetch.cpp
    src/lua_ctx/lua_ctx_ls.cpp
    src/lua_ctx/lua_ctx_move.cpp
    src/lua_ctx/lua_ctx_run.cpp
    src/lua_ctx/lua_ctx_product.cpp
    src/phases/phase_build.cpp
    src/phases/phase_check.cpp
    src/phases/phase_completion.cpp
    src/phases/phase_deploy.cpp
    src/phases/phase_fetch.cpp
    src/phases/phase_install.cpp
    src/phases/phase_recipe_fetch.cpp
    src/phases/phase_stage.cpp
    src/extract.cpp
    src/aws_util.cpp
    src/blake3_util.cpp
    src/libgit2_util.cpp
    src/sol_util.cpp
    src/lua_shell.cpp
    src/lua_envy.cpp
    src/product_util.cpp
    src/lua_error_formatter.cpp
    src/manifest.cpp
    src/recipe_phase.cpp
    src/recipe_spec.cpp
    $<$<PLATFORM_ID:Windows>:src/sha256_win.cpp>
    $<$<NOT:$<PLATFORM_ID:Windows>>:src/sha256_mbedtls.cpp>
    src/fetch.cpp
    src/libcurl_util.cpp
    src/platform.cpp
    src/shell.cpp
    $<$<PLATFORM_ID:Windows>:src/shell_win32.cpp>
    $<$<NOT:$<PLATFORM_ID:Windows>>:src/shell_posix.cpp>
    src/trace.cpp
    src/tui.cpp
    src/uri.cpp
    src/util.cpp
)

set(ENVY_UNIT_TEST_SOURCES
    src/unit_test_main.cpp
    src/blake3_util_tests.cpp
    src/cache_tests.cpp
    src/cli_tests.cpp
    src/cmds/cmd_tests.cpp
    src/phases/phase_check_tests.cpp
    src/phases/phase_fetch_tests.cpp
    src/phases/phase_install_tests.cpp
    src/cmds/cmd_extract_tests.cpp
    src/cmds/cmd_lua_tests.cpp
    src/cmds/cmd_version_tests.cpp
    src/extract_tests.cpp
    src/engine_tests.cpp
    src/engine_weak_resolution_tests.cpp
    src/engine_target_phase_tests.cpp
    src/fetch_tests.cpp
    src/lua_error_formatter_tests.cpp
    src/lua_shell_tests.cpp
    src/manifest_tests.cpp
    src/recipe_spec_tests.cpp
    src/recipe_spec_custom_source_tests.cpp
    src/recipe_key_tests.cpp
    src/lua_ctx/lua_ctx_bindings_tests.cpp
    src/lua_ctx/lua_ctx_copy_tests.cpp
    src/lua_ctx/lua_ctx_fetch_tests.cpp
    src/lua_ctx_asset_product_tests.cpp
    src/lua_ctx/lua_ctx_move_tests.cpp
    src/product_util_tests.cpp
    src/sha256_tests.cpp
    src/shell_tests_common.cpp
    $<$<PLATFORM_ID:Windows>:src/shell_tests_win32.cpp>
    $<$<NOT:$<PLATFORM_ID:Windows>>:src/shell_tests_posix.cpp>
    src/uri_tests.cpp
    src/tui_tests.cpp
    src/util_tests.cpp
)

add_executable(envy_unit_tests)
target_sources(envy_unit_tests PRIVATE ${ENVY_UNIT_TEST_COMMON_SOURCES} ${ENVY_UNIT_TEST_SOURCES})
target_compile_definitions(envy_unit_tests PRIVATE ENVY_VERSION_STR=\"${PROJECT_VERSION}\")
target_link_libraries(envy_unit_tests PRIVATE doctest::doctest)
# GCC: Disable ASan to avoid AWS SDK hashlittle2 false positive (LTO inlines across libs)
# Clang: Use -fsanitize-blacklist (configured in configure_envy_executable)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    configure_envy_executable(envy_unit_tests "undefined")
else()
    configure_envy_executable(envy_unit_tests "address,undefined")
endif()

if(DEFINED _envy_rescan_link_group)
    set(_envy_link_targets envy envy_unit_tests)
    if(MSVC)
        list(APPEND _envy_link_targets envy_functional_tester)
    else()
        list(APPEND _envy_link_targets envy_functional_tester_asan_ubsan envy_functional_tester_tsan_ubsan)
    endif()
    foreach(_envy_target IN LISTS _envy_link_targets)
        if(TARGET ${_envy_target})
            target_link_libraries(${_envy_target}
                PRIVATE
                    "${_envy_rescan_link_group}"
            )
        endif()
    endforeach()
    unset(_envy_link_targets)
    unset(_envy_rescan_link_group)
endif()

# Run tests and create timestamp on success
# MSVC ASan doesn't use the same suppression file format, so only pass ASAN_OPTIONS on Clang/GCC
if(MSVC)
    set(_envy_test_env_prefix)
else()
    set(_envy_test_env_prefix ${CMAKE_COMMAND} -E env "ASAN_OPTIONS=suppressions=${PROJECT_SOURCE_DIR}/asan.supp")
endif()
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/envy_unit_tests.timestamp"
    COMMAND ${_envy_test_env_prefix} $<TARGET_FILE:envy_unit_tests>
    COMMAND ${CMAKE_COMMAND} -E touch "${CMAKE_CURRENT_BINARY_DIR}/envy_unit_tests.timestamp"
    DEPENDS envy_unit_tests ${TEST_DATA_FILES}
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    COMMENT "Running unit tests"
    VERBATIM
)
unset(_envy_test_env_prefix)

add_custom_target(run_unit_tests ALL
    DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/envy_unit_tests.timestamp"
)

# Functional test executables are built but not run automatically
# Run manually with: python -m functional_tests

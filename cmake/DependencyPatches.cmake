# Helper utilities for patching third-party projects in-flight.  Each helper
# script rewrites fragile upstream build logic just enough to fit Envy’s
# monorepo layout without forking entire trees.  Keep the interventions
# narrowly scoped and idempotent so reconfiguration remains safe.

if(NOT DEFINED ENVY_PYTHON_LAUNCHER)
    if(WIN32)
        find_program(_envy_python_launcher py REQUIRED)
        set(ENVY_PYTHON_ARGS "-3" CACHE INTERNAL "Arguments passed to the Python launcher" FORCE)
    else()
        find_program(_envy_python_launcher python3 REQUIRED)
        set(ENVY_PYTHON_ARGS "" CACHE INTERNAL "Arguments passed to the Python interpreter" FORCE)
    endif()
    set(ENVY_PYTHON_LAUNCHER "${_envy_python_launcher}" CACHE INTERNAL "Python entrypoint for Envy CMake helpers" FORCE)
    unset(_envy_python_launcher)
endif()

function(envy_run_python script)
    if(NOT EXISTS "${script}")
        message(FATAL_ERROR "Python script '${script}' not found")
    endif()

    set(_envy_python_command "${ENVY_PYTHON_LAUNCHER}")
    if(ENVY_PYTHON_ARGS)
        list(APPEND _envy_python_command ${ENVY_PYTHON_ARGS})
    endif()
    list(APPEND _envy_python_command "${script}")
    if(ARGN)
        list(APPEND _envy_python_command ${ARGN})
    endif()

    execute_process(COMMAND ${_envy_python_command}
        COMMAND_ERROR_IS_FATAL ANY)

    unset(_envy_python_command)
endfunction()

function(envy_patch_libssh2 source_dir binary_dir)
    set(_source_dir_norm "${source_dir}")
    set(_binary_dir_norm "${binary_dir}")

    set(_stamp "${_binary_dir_norm}/envy_libssh2_patch.stamp")
    if(EXISTS "${_stamp}")
        return()
    endif()

    set(_script "${_binary_dir_norm}/envy_patch_libssh2.py")
    set(_template "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/templates/libssh2_patch.py.in")

    set(LIBSSH2_CMAKELISTS "${_source_dir_norm}/CMakeLists.txt")
    configure_file("${_template}" "${_script}" @ONLY)

    envy_run_python("${_script}")

    file(REMOVE "${_script}")
    file(WRITE "${_stamp}" "patched\n")

    unset(_stamp)
    unset(_script)
    unset(_template)
    unset(_source_dir_norm)
    unset(_binary_dir_norm)
endfunction()

function(envy_patch_libgit2_select libgit2_source_dir libgit2_binary_dir libssh2_source_dir libssh2_binary_dir)
    set(_source_dir_norm "${libgit2_source_dir}")
    set(_binary_dir_norm "${libgit2_binary_dir}")

    set(_stamp "${_binary_dir_norm}/envy_libgit2_select_patch.stamp")
    if(EXISTS "${_stamp}")
        return()
    endif()

    set(_script "${_binary_dir_norm}/envy_patch_libgit2_select.py")
    set(_template "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/templates/libgit2_select_patch.py.in")

    set(SELECT_PATH "${_source_dir_norm}/cmake/SelectSSH.cmake")
    if(NOT EXISTS "${SELECT_PATH}")
        return()
    endif()

    set(LIBSSH2_SOURCE "${libssh2_source_dir}")
    set(LIBSSH2_BINARY "${libssh2_binary_dir}")
    configure_file("${_template}" "${_script}" @ONLY)

    envy_run_python("${_script}")

    file(REMOVE "${_script}")
    file(WRITE "${_stamp}" "patched\n")

    unset(_stamp)
    unset(_script)
    unset(_template)
    unset(_source_dir_norm)
    unset(_binary_dir_norm)
    unset(SELECT_PATH)
    unset(LIBSSH2_SOURCE)
    unset(LIBSSH2_BINARY)
endfunction()

function(envy_patch_libarchive_cmakelists source_dir binary_dir)
    set(_source_dir_norm "${source_dir}")
    set(_binary_dir_norm "${binary_dir}")

    set(_stamp "${_binary_dir_norm}/envy_libarchive_patch.stamp")
    if(EXISTS "${_stamp}")
        return()
    endif()

    set(_script "${_binary_dir_norm}/envy_patch_libarchive.py")
    set(_template "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/templates/libarchive_patch.py.in")

    set(LIBARCHIVE_CMAKELISTS "${_source_dir_norm}/CMakeLists.txt")
    set(LIBARCHIVE_SOURCE "${_source_dir_norm}/libarchive")
    if(NOT EXISTS "${LIBARCHIVE_CMAKELISTS}")
        return()
    endif()

    configure_file("${_template}" "${_script}" @ONLY)

    envy_run_python("${_script}")

    file(REMOVE "${_script}")
    file(WRITE "${_stamp}" "patched\n")

    unset(_stamp)
    unset(_script)
    unset(_template)
    unset(_source_dir_norm)
    unset(_binary_dir_norm)
    unset(LIBARCHIVE_CMAKELISTS)
    unset(LIBARCHIVE_SOURCE)
endfunction()

function(envy_patch_aws_sdk_curl source_dir binary_dir)
    set(_source_dir_norm "${source_dir}")
    set(_binary_dir_norm "${binary_dir}")

    set(_stamp "${_binary_dir_norm}/envy_awssdk_curl_patch.stamp")
    if(EXISTS "${_stamp}")
        return()
    endif()

    set(_script "${_binary_dir_norm}/envy_patch_aws_sdk_curl.py")
    set(_template "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/templates/aws_sdk_curl_patch.py.in")

    set(AWS_CORE_CMAKELISTS "${_source_dir_norm}/src/aws-cpp-sdk-core/CMakeLists.txt")
    if(NOT EXISTS "${AWS_CORE_CMAKELISTS}")
        return()
    endif()

    configure_file("${_template}" "${_script}" @ONLY)

    envy_run_python("${_script}")

    file(REMOVE "${_script}")
    file(WRITE "${_stamp}" "patched\n")

    unset(_stamp)
    unset(_script)
    unset(_template)
    unset(_source_dir_norm)
    unset(_binary_dir_norm)
    unset(AWS_CORE_CMAKELISTS)
endfunction()

function(envy_patch_aws_sdk_runtime source_dir binary_dir)
    set(_source_dir_norm "${source_dir}")
    set(_binary_dir_norm "${binary_dir}")

    set(_stamp "${_binary_dir_norm}/envy_awssdk_runtime_patch.stamp")
    if(EXISTS "${_stamp}")
        return()
    endif()

    set(_script "${_binary_dir_norm}/envy_patch_aws_sdk_runtime.py")
    set(_template "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/templates/aws_sdk_runtime_patch.py.in")

    set(AWS_CFLAGS_PATH "${_source_dir_norm}/crt/aws-crt-cpp/crt/aws-c-common/cmake/AwsCFlags.cmake")
    if(NOT EXISTS "${AWS_CFLAGS_PATH}")
        return()
    endif()

    configure_file("${_template}" "${_script}" @ONLY)

    envy_run_python("${_script}")

    file(REMOVE "${_script}")
    file(WRITE "${_stamp}" "patched\n")

    unset(_stamp)
    unset(_script)
    unset(_template)
    unset(_source_dir_norm)
    unset(_binary_dir_norm)
    unset(AWS_CFLAGS_PATH)
endfunction()

function(envy_patch_libcurl_cmakelists source_dir binary_dir zlib_target)
    set(_source_dir_norm "${source_dir}")
    set(_binary_dir_norm "${binary_dir}")

    set(_stamp "${_binary_dir_norm}/envy_libcurl_patch.stamp")
    if(EXISTS "${_stamp}")
        return()
    endif()

    set(_script "${_binary_dir_norm}/envy_patch_libcurl.py")
    set(_template "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/templates/libcurl_patch.py.in")

    set(LIBCURL_CMAKELISTS "${_source_dir_norm}/CMakeLists.txt")
    set(ENVY_ZLIB_TARGET "${zlib_target}")
    if(NOT EXISTS "${LIBCURL_CMAKELISTS}")
        return()
    endif()

    configure_file("${_template}" "${_script}" @ONLY)

    envy_run_python("${_script}")

    file(REMOVE "${_script}")
    file(WRITE "${_stamp}" "patched\n")

    unset(_stamp)
    unset(_script)
    unset(_template)
    unset(_source_dir_norm)
    unset(_binary_dir_norm)
    unset(LIBCURL_CMAKELISTS)
    unset(ENVY_ZLIB_TARGET)
endfunction()

function(envy_patch_libgit2_nsec source_dir binary_dir)
    set(_source_dir_norm "${source_dir}")
    set(_binary_dir_norm "${binary_dir}")

    set(_stamp "${_binary_dir_norm}/envy_libgit2_nsec_patch.stamp")
    if(EXISTS "${_stamp}")
        return()
    endif()

    set(_script "${_binary_dir_norm}/envy_patch_libgit2_nsec.py")
    set(_template "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/templates/libgit2_nsec_patch.py.in")

    set(FIND_STAT_NSEC "${_source_dir_norm}/cmake/FindStatNsec.cmake")
    if(NOT EXISTS "${FIND_STAT_NSEC}")
        return()
    endif()

    configure_file("${_template}" "${_script}" @ONLY)

    envy_run_python("${_script}")

    file(REMOVE "${_script}")
    file(WRITE "${_stamp}" "patched\n")

    unset(_stamp)
    unset(_script)
    unset(_template)
    unset(_source_dir_norm)
    unset(_binary_dir_norm)
    unset(FIND_STAT_NSEC)
endfunction()

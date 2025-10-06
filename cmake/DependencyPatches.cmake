# Helper utilities for applying one-off patches to third-party sources.

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

    execute_process(COMMAND python3 "${_script}"
        COMMAND_ERROR_IS_FATAL ANY)

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

    execute_process(COMMAND python3 "${_script}"
        COMMAND_ERROR_IS_FATAL ANY)

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
    if(NOT EXISTS "${LIBARCHIVE_CMAKELISTS}")
        return()
    endif()

    configure_file("${_template}" "${_script}" @ONLY)

    execute_process(COMMAND python3 "${_script}"
        COMMAND_ERROR_IS_FATAL ANY)

    file(REMOVE "${_script}")
    file(WRITE "${_stamp}" "patched\n")

    unset(_stamp)
    unset(_script)
    unset(_template)
    unset(_source_dir_norm)
    unset(_binary_dir_norm)
    unset(LIBARCHIVE_CMAKELISTS)
endfunction()

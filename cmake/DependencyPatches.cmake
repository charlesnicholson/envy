# Helper utilities for applying one-off patches to third-party sources.

function(codex_patch_libssh2 source_dir binary_dir)
    set(_source_dir_norm "${source_dir}")
    set(_binary_dir_norm "${binary_dir}")

    set(_stamp "${_binary_dir_norm}/codex_libssh2_patch.stamp")
    if(EXISTS "${_stamp}")
        return()
    endif()

    set(_script "${_binary_dir_norm}/codex_patch_libssh2.py")
    set(_template "${CMAKE_CURRENT_LIST_DIR}/templates/libssh2_patch.py.in")

    set(LIBSSH2_CMAKELISTS "${_source_dir_norm}/CMakeLists.txt")
    configure_file("${_template}" "${_script}" @ONLY)

    execute_process(COMMAND python3 "${_script}"
        COMMAND_ERROR_IS_FATAL ANY)

    file(REMOVE "${_script}")
    file(WRITE "${_stamp}" "patched
")

    unset(_stamp)
    unset(_script)
    unset(_template)
    unset(_source_dir_norm)
    unset(_binary_dir_norm)
endfunction()

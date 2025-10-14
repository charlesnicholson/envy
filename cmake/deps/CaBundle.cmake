set(ENVY_CACERT_URL "https://curl.se/ca/cacert.pem")
set(ENVY_CACERT_SHA256 f290e6acaf904a4121424ca3ebdd70652780707e28e8af999221786b86bb1975)

cmake_path(APPEND ENVY_THIRDPARTY_CACHE_DIR "cacert-${ENVY_CACERT_SHA256}.pem" OUTPUT_VARIABLE _envy_ca_bundle_path)
cmake_path(GET _envy_ca_bundle_path PARENT_PATH _envy_ca_bundle_dir)
file(MAKE_DIRECTORY "${_envy_ca_bundle_dir}")

if(NOT EXISTS "${_envy_ca_bundle_path}")
    set(_envy_ca_tmp "${_envy_ca_bundle_path}.tmp")
    file(DOWNLOAD
        "${ENVY_CACERT_URL}"
        "${_envy_ca_tmp}"
        EXPECTED_HASH SHA256=${ENVY_CACERT_SHA256}
        TLS_VERIFY ON)
    file(RENAME "${_envy_ca_tmp}" "${_envy_ca_bundle_path}")
endif()

cmake_path(CONVERT "${_envy_ca_bundle_path}" TO_CMAKE_PATH_LIST _envy_ca_bundle_norm)
set(ENVY_CA_BUNDLE "${_envy_ca_bundle_norm}" CACHE FILEPATH "Pinned CA bundle for TLS consumers" FORCE)

unset(_envy_ca_tmp)
unset(_envy_ca_bundle_dir)
unset(_envy_ca_bundle_path)
unset(_envy_ca_bundle_norm)

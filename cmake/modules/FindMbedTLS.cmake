if(DEFINED ENVY_MBEDTLS_BINARY_DIR AND DEFINED ENVY_MBEDTLS_SOURCE_DIR)
    set(_envy_mbedtls_include "${ENVY_MBEDTLS_SOURCE_DIR}/include")
    set(_envy_mbedtls_libdir "${ENVY_MBEDTLS_BINARY_DIR}/library")
elseif(DEFINED mbedtls_BINARY_DIR AND DEFINED mbedtls_SOURCE_DIR)
    set(_envy_mbedtls_include "${mbedtls_SOURCE_DIR}/include")
    set(_envy_mbedtls_libdir "${mbedtls_BINARY_DIR}/library")
endif()

if(DEFINED _envy_mbedtls_include AND DEFINED _envy_mbedtls_libdir)
    set(MBEDTLS_INCLUDE_DIR "${_envy_mbedtls_include}" CACHE PATH "" FORCE)
    set(MBEDTLS_INCLUDE_DIRS "${_envy_mbedtls_include}" CACHE STRING "" FORCE)
    set(MBEDTLS_LIBRARIES
        "${_envy_mbedtls_libdir}/libmbedtls.a"
        "${_envy_mbedtls_libdir}/libmbedx509.a"
        "${_envy_mbedtls_libdir}/libmbedcrypto.a"
    )
    set(MBEDCRYPTO_LIBRARY "${_envy_mbedtls_libdir}/libmbedcrypto.a" CACHE FILEPATH "" FORCE)
    if(DEFINED ENVY_MBEDTLS_VERSION)
        set(MBEDTLS_VERSION "${ENVY_MBEDTLS_VERSION}")
    endif()
    set(MBEDTLS_FOUND TRUE)
    unset(_envy_mbedtls_include)
    unset(_envy_mbedtls_libdir)
    return()
endif()

unset(_envy_mbedtls_include)
unset(_envy_mbedtls_libdir)

find_path(MBEDTLS_INCLUDE_DIR NAMES "mbedtls/version.h")
find_library(MBEDTLS_LIB_MBEDTLS NAMES mbedtls libmbedtls)
find_library(MBEDTLS_LIB_MBEDX509 NAMES mbedx509 libmbedx509)
find_library(MBEDTLS_LIB_MBEDCRYPTO NAMES mbedcrypto libmbedcrypto)

if(MBEDTLS_INCLUDE_DIR)
    set(MBEDTLS_INCLUDE_DIRS "${MBEDTLS_INCLUDE_DIR}")
endif()

if(MBEDTLS_LIB_MBEDTLS AND MBEDTLS_LIB_MBEDX509 AND MBEDTLS_LIB_MBEDCRYPTO)
    set(MBEDTLS_LIBRARIES
        "${MBEDTLS_LIB_MBEDTLS}"
        "${MBEDTLS_LIB_MBEDX509}"
        "${MBEDTLS_LIB_MBEDCRYPTO}"
    )
    set(MBEDCRYPTO_LIBRARY "${MBEDTLS_LIB_MBEDCRYPTO}" CACHE FILEPATH "" FORCE)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(MbedTLS
    REQUIRED_VARS MBEDTLS_INCLUDE_DIR MBEDTLS_LIB_MBEDTLS MBEDTLS_LIB_MBEDX509 MBEDTLS_LIB_MBEDCRYPTO)

set(AWS_SDK_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(AWS_SDK_CPP_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(AWS_SDK_CPP_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(AWS_SDK_CPP_CUSTOM_MEMORY_MANAGEMENT OFF CACHE BOOL "" FORCE)
set(BUILD_ONLY "s3;sso;sso-oidc" CACHE STRING "" FORCE)
set(AWS_BUILD_ONLY "s3;sso;sso-oidc" CACHE STRING "" FORCE)
set(AWS_SDK_CPP_BUILD_ONLY "s3;sso;sso-oidc" CACHE STRING "" FORCE)
set(ENFORCE_SUBMODULE_VERSIONS OFF CACHE BOOL "" FORCE)
cmake_path(APPEND ENVY_THIRDPARTY_CACHE_DIR "${ENVY_AWS_SDK_ARCHIVE}" OUTPUT_VARIABLE _aws_sdk_archive)
set(_aws_sdk_url "${ENVY_AWS_SDK_URL}")
if(EXISTS "${_aws_sdk_archive}")
    file(TO_CMAKE_PATH "${_aws_sdk_archive}" _aws_sdk_archive_norm)
    set(_aws_sdk_url "file://${_aws_sdk_archive_norm}")
endif()

cmake_path(APPEND ENVY_THIRDPARTY_CACHE_DIR "aws_sdk-src" OUTPUT_VARIABLE aws_sdk_SOURCE_DIR)
cmake_path(APPEND CMAKE_BINARY_DIR "_deps" "aws_sdk-build" OUTPUT_VARIABLE aws_sdk_BINARY_DIR)

if(NOT EXISTS "${aws_sdk_SOURCE_DIR}/CMakeLists.txt")
    FetchContent_Populate(aws_sdk
        SOURCE_DIR "${aws_sdk_SOURCE_DIR}"
        BINARY_DIR "${aws_sdk_BINARY_DIR}"
        URL ${_aws_sdk_url}
        URL_HASH SHA256=${ENVY_AWS_SDK_SHA256}
    )
endif()

unset(_aws_sdk_archive)
unset(_aws_sdk_archive_norm)
unset(_aws_sdk_url)

set(ENVY_AWSCRT_ROOT "${aws_sdk_SOURCE_DIR}/crt/aws-crt-cpp")
execute_process(
    COMMAND ${CMAKE_COMMAND}
        -Daws_sdk_SOURCE_DIR=${aws_sdk_SOURCE_DIR}
        -Denvy_thirdparty_cache_dir=${ENVY_THIRDPARTY_CACHE_DIR}
        -P "${PROJECT_SOURCE_DIR}/cmake/scripts/prefetch_aws_crt.cmake"
    WORKING_DIRECTORY "${aws_sdk_SOURCE_DIR}"
    COMMAND_ERROR_IS_FATAL ANY)
if(NOT EXISTS "${ENVY_AWSCRT_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR "AWS CRT prefetch failed: missing ${ENVY_AWSCRT_ROOT}/CMakeLists.txt")
endif()

set(_envy_prev_build_testing ${BUILD_TESTING})
set(BUILD_TESTING OFF CACHE BOOL "Disable tests inside AWS SDK build" FORCE)
add_subdirectory(${aws_sdk_SOURCE_DIR} ${aws_sdk_BINARY_DIR})
if(TARGET s2n)
    set_property(TARGET s2n PROPERTY INTERPROCEDURAL_OPTIMIZATION OFF)
    target_compile_options(s2n PRIVATE -Wno-builtin-macro-redefined)
endif()
if(TARGET aws-c-common)
    target_compile_options(aws-c-common PRIVATE -Wno-builtin-macro-redefined)
endif()
if(TARGET aws-c-io)
    target_compile_options(aws-c-io PRIVATE -Wno-builtin-macro-redefined)
endif()
set(BUILD_TESTING ${_envy_prev_build_testing} CACHE BOOL "Restart project testing flag" FORCE)
unset(_envy_prev_build_testing)

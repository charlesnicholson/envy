cmake_minimum_required(VERSION 4.1.2)

if(NOT DEFINED aws_sdk_SOURCE_DIR)
  message(FATAL_ERROR "aws_sdk_SOURCE_DIR must be provided")
endif()

if(NOT DEFINED envy_project_source_dir)
  message(FATAL_ERROR "envy_project_source_dir must be provided")
endif()

set(_path "$ENV{PATH}")
if(_path)
  set(_path "${envy_project_source_dir}/tools:${_path}")
else()
  set(_path "${envy_project_source_dir}/tools")
endif()
set(_orig_path "$ENV{PATH}")
set(ENV{PATH} "${_path}")
execute_process(
  COMMAND bash "${aws_sdk_SOURCE_DIR}/prefetch_crt_dependency.sh"
  WORKING_DIRECTORY "${aws_sdk_SOURCE_DIR}"
  COMMAND_ERROR_IS_FATAL ANY
)
if(_orig_path)
  set(ENV{PATH} "${_orig_path}")
else()
  unset(ENV{PATH})
endif()

set(_crt_root "${aws_sdk_SOURCE_DIR}/crt/aws-crt-cpp")
set(_crt_tmp "${_crt_root}/crt/tmp")
if(EXISTS "${_crt_tmp}")
  file(REMOVE_RECURSE "${_crt_tmp}")
endif()

unset(_path)
unset(_crt_root)
unset(_crt_tmp)

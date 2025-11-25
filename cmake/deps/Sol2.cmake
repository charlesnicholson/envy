include_guard(GLOBAL)

cmake_path(APPEND ENVY_CACHE_DIR "sol2-src" OUTPUT_VARIABLE sol2_SOURCE_DIR)
cmake_path(APPEND CMAKE_BINARY_DIR "_deps" "sol2-build" OUTPUT_VARIABLE sol2_BINARY_DIR)

# Fetch sol2 if not cached
if(NOT EXISTS "${sol2_SOURCE_DIR}/.git")
  message(STATUS "[envy] Fetching sol2 from GitHub...")

  find_package(Git REQUIRED)

  execute_process(
    COMMAND "${GIT_EXECUTABLE}" clone https://github.com/ThePhD/sol2.git "${sol2_SOURCE_DIR}"
    RESULT_VARIABLE GIT_CLONE_RESULT
    OUTPUT_VARIABLE GIT_CLONE_OUTPUT
    ERROR_VARIABLE GIT_CLONE_ERROR
  )

  if(NOT GIT_CLONE_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to clone sol2: ${GIT_CLONE_ERROR}")
  endif()

  execute_process(
    COMMAND "${GIT_EXECUTABLE}" checkout ${ENVY_SOL2_GIT_TAG}
    WORKING_DIRECTORY "${sol2_SOURCE_DIR}"
    RESULT_VARIABLE GIT_CHECKOUT_RESULT
    OUTPUT_VARIABLE GIT_CHECKOUT_OUTPUT
    ERROR_VARIABLE GIT_CHECKOUT_ERROR
  )

  if(NOT GIT_CHECKOUT_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to checkout sol2 commit ${ENVY_SOL2_GIT_TAG}: ${GIT_CHECKOUT_ERROR}")
  endif()
else()
  message(STATUS "[envy] Reusing cached sol2 sources at ${sol2_SOURCE_DIR}")
endif()

# Generate amalgamation
set(SOL2_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
set(SOL2_AMALGAMATION "${SOL2_GENERATED_DIR}/sol/sol.hpp")

file(MAKE_DIRECTORY "${SOL2_GENERATED_DIR}/sol")

if(NOT EXISTS "${SOL2_AMALGAMATION}")
  message(STATUS "[envy] Generating sol2 amalgamation...")
  execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${sol2_SOURCE_DIR}/single/single.py"
            "--output" "${SOL2_AMALGAMATION}"
    WORKING_DIRECTORY "${sol2_SOURCE_DIR}"
    RESULT_VARIABLE SOL2_AMALGAMATE_RESULT
    OUTPUT_VARIABLE SOL2_AMALGAMATE_OUTPUT
    ERROR_VARIABLE SOL2_AMALGAMATE_ERROR
  )

  if(NOT SOL2_AMALGAMATE_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to generate sol2 amalgamation:\n${SOL2_AMALGAMATE_ERROR}")
  endif()
else()
  message(STATUS "[envy] Reusing cached sol2 amalgamation at ${SOL2_AMALGAMATION}")
endif()

# Verify amalgamation exists
if(NOT EXISTS "${SOL2_AMALGAMATION}")
  message(FATAL_ERROR "Sol2 amalgamation not generated at ${SOL2_AMALGAMATION}")
endif()

# Create interface target
if(NOT TARGET sol2::sol2)
  add_library(sol2 INTERFACE)
  target_include_directories(sol2 INTERFACE "${SOL2_GENERATED_DIR}")
  target_link_libraries(sol2 INTERFACE lua::lua)
  add_library(sol2::sol2 ALIAS sol2)
endif()

# Sol2 auto-detects Lua vs LuaJIT from headers - no config needed
# Envy uses standard Lua 5.4, not LuaJIT

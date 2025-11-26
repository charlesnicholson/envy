# Sol2 Integration Migration Plan

## Overview

Replace raw Lua C API with sol2 (c1f95a773c6f8f4fde8ca3efe872e7286afe4444) for type-safe C++/Lua bindings. Sol2 eliminates manual stack manipulation, provides compile-time safety, STL interop, and idiomatic C++ patterns. Implementation proceeds in dependency-ordered phases with full test coverage at each step.

**Key decisions:**
- Use `sol::state` directly (no wrapper abstraction)
- Migrate `lua_value` → `sol::object` throughout
- Single atomic changeset documented as sequential phases
- Accept < 5% binary size increase

---

## Phase 1: Add Sol2 Dependency + Generate Amalgamation

### Objective
Integrate sol2 as header-only dependency via CMake FetchContent; generate `sol.hpp` amalgamation using `single.py` script from upstream repo.

### Files Created
- `cmake/deps/Sol2.cmake` - FetchContent configuration
- `${CMAKE_BINARY_DIR}/generated/sol2/sol.hpp` - Generated amalgamation (build-time)

### Implementation

**cmake/deps/Sol2.cmake:**
```cmake
include_guard(GLOBAL)

include(FetchContent)
include(EnvyFetchContent)

envy_fetch_content_declare(
  sol2
  GIT_REPOSITORY https://github.com/ThePhD/sol2.git
  GIT_TAG c1f95a773c6f8f4fde8ca3efe872e7286afe4444
  GIT_SHALLOW FALSE
)

envy_fetch_content_make_available(sol2)

# Generate amalgamation
set(SOL2_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated/sol2")
set(SOL2_AMALGAMATION "${SOL2_GENERATED_DIR}/sol.hpp")

file(MAKE_DIRECTORY "${SOL2_GENERATED_DIR}")

execute_process(
  COMMAND "${Python3_EXECUTABLE}" "${sol2_SOURCE_DIR}/single/single.py"
          "--output" "${SOL2_AMALGAMATION}"
          "--single"
  WORKING_DIRECTORY "${sol2_SOURCE_DIR}"
  RESULT_VARIABLE SOL2_AMALGAMATE_RESULT
  OUTPUT_VARIABLE SOL2_AMALGAMATE_OUTPUT
  ERROR_VARIABLE SOL2_AMALGAMATE_ERROR
)

if(NOT SOL2_AMALGAMATE_RESULT EQUAL 0)
  message(FATAL_ERROR "Failed to generate sol2 amalgamation:\n${SOL2_AMALGAMATE_ERROR}")
endif()

# Verify amalgamation exists
if(NOT EXISTS "${SOL2_AMALGAMATION}")
  message(FATAL_ERROR "Sol2 amalgamation not generated at ${SOL2_AMALGAMATION}")
endif()

# Create interface target
add_library(sol2::sol2 INTERFACE IMPORTED GLOBAL)
target_include_directories(sol2::sol2 INTERFACE "${SOL2_GENERATED_DIR}")
target_link_libraries(sol2::sol2 INTERFACE lua::lua)

# Sol2 auto-detects Lua vs LuaJIT from headers - no config needed
# Envy uses standard Lua 5.4, not LuaJIT
```

**cmake/Dependencies.cmake** (add after lua):
```cmake
include(deps/Sol2)
target_link_libraries(envy_common INTERFACE sol2::sol2)
```

**docs/dependencies.md** (append):
```markdown
### Sol2

**Version:** c1f95a773c6f8f4fde8ca3efe872e7286afe4444
**License:** MIT
**Purpose:** Header-only C++/Lua bindings—type-safe stack manipulation, STL interop
**Integration:** Amalgamation generated via `single.py` during CMake configure
**Rationale:** Eliminates error-prone manual stack manipulation; enables idiomatic C++ patterns for Lua integration
```

### Validation
- Amalgamation generated: `ls out/build/generated/sol2/sol.hpp`
- Include test: Add `#include <sol/sol.hpp>` to `src/manifest.h`
- Build succeeds: `./build.sh` (verifies header compiles and links)
- Binary size baseline: `du -h out/build/envy` (record for Phase 2+ comparison)

---

## Phase 2: Delete lua_util, Create lua_envy, Migrate to Direct Sol2

### Objective
Delete `lua_util.h/cpp` entirely; create minimal `lua_envy.h/cpp` for envy globals setup; migrate all lua_util usage throughout codebase to direct sol2 calls.

### Files Deleted
- `src/lua_util.h`
- `src/lua_util.cpp`
- `src/lua_util_tests.cpp`

### Files Created
- `src/lua_envy.h` - Single function declaration
- `src/lua_envy.cpp` - Envy globals setup implementation

### Files Modified
- All files that `#include "lua_util.h"` (~30+ files)
  - Replace with `#include <sol/sol.hpp>` and optionally `#include "lua_envy.h"`
  - Replace all lua_util function calls with direct sol2 equivalents

### New lua_envy Implementation

**lua_envy.h:**
```cpp
#pragma once

#include <sol/sol.hpp>

namespace envy {

// Install envy globals, platform constants, and custom functions into Lua state
void lua_envy_install(sol::state &lua);

}  // namespace envy
```

**lua_envy.cpp (extracted from old lua_util.cpp):**
```cpp
#include "lua_envy.h"
#include "shell.h"
#include "tui.h"

#include <sstream>

namespace envy {
namespace {

constexpr char kEnvyTemplateLua[] = R"lua(
return function(str, values)
  if type(str) ~= "string" then
    error("envy.template: first argument must be a string", 2)
  end
  if type(values) ~= "table" then
    error("envy.template: second argument must be a table", 2)
  end

  local function normalize_key(raw)
    local trimmed = raw:match("^%s*(.-)%s*$")
    if not trimmed or trimmed == "" then
      error("envy.template: placeholder cannot be empty", 2)
    end
    if not trimmed:match("^[%a_][%w_]*$") then
      error("envy.template: placeholder '" .. trimmed .. "' contains invalid characters", 2)
    end
    return trimmed
  end

  local function ensure_pairs(str)
    local open_count = 0
    local i = 1
    while i <= #str do
      if str:sub(i, i+1) == "{{" then
        open_count = open_count + 1
        i = i + 2
      elseif str:sub(i, i+1) == "}}" then
        open_count = open_count - 1
        if open_count < 0 then
          error("envy.template: unmatched '}}' at position " .. i, 2)
        end
        i = i + 2
      else
        i = i + 1
      end
    end
    if open_count > 0 then
      error("envy.template: unmatched '{{' (missing closing '}}')", 2)
    end
  end

  ensure_pairs(str)

  local function replacer(token)
    local key = normalize_key(token)
    local value = values[key]
    if value == nil then
      error("envy.template: missing value for placeholder '" .. key .. "'", 2)
    end
    return tostring(value)
  end

  return (str:gsub("{{(.-)}}", replacer))
end
)lua";

}  // namespace

void lua_envy_install(sol::state &lua) {
  // Platform detection
  char const *platform{ nullptr };
  char const *arch{ nullptr };

#if defined(__APPLE__) && defined(__MACH__)
  platform = "darwin";
  #if defined(__arm64__)
    arch = "arm64";
  #elif defined(__x86_64__)
    arch = "x86_64";
  #endif
#elif defined(__linux__)
  platform = "linux";
  #if defined(__aarch64__)
    arch = "aarch64";
  #elif defined(__x86_64__)
    arch = "x86_64";
  #endif
#elif defined(_WIN32)
  platform = "windows";
  #if defined(_M_ARM64)
    arch = "arm64";
  #elif defined(_M_X64)
    arch = "x86_64";
  #endif
#endif

  std::string const platform_arch{ std::string{platform} + "-" + arch };

  // Platform globals
  lua["ENVY_PLATFORM"] = platform;
  lua["ENVY_ARCH"] = arch;
  lua["ENVY_PLATFORM_ARCH"] = platform_arch;

  // Override print to route through TUI
  lua["print"] = [](sol::variadic_args va) {
    std::ostringstream oss;
    bool first{ true };
    for (auto arg : va) {
      if (!first) oss << '\t';
      // Use luaL_tolstring for consistent formatting
      oss << luaL_tolstring(arg.lua_state(), arg.stack_index(), nullptr);
      lua_pop(arg.lua_state(), 1);  // Pop result from luaL_tolstring
      first = false;
    }
    tui::info("%s", oss.str().c_str());
  };

  // envy table with logging functions
  auto envy_table = lua.create_table();
  envy_table["trace"] = [](std::string_view msg) { tui::debug("%s", msg.data()); };
  envy_table["debug"] = [](std::string_view msg) { tui::debug("%s", msg.data()); };
  envy_table["info"] = [](std::string_view msg) { tui::info("%s", msg.data()); };
  envy_table["warn"] = [](std::string_view msg) { tui::warn("%s", msg.data()); };
  envy_table["error"] = [](std::string_view msg) { tui::error("%s", msg.data()); };
  envy_table["stdout"] = [](std::string_view msg) { tui::print_stdout("%s", msg.data()); };

  // envy.template (load Lua code)
  sol::protected_function_result result =
      lua.safe_script(kEnvyTemplateLua, sol::script_pass_on_error);
  if (result.valid()) {
    envy_table["template"] = result;
  } else {
    sol::error err = result;
    tui::error("Failed to load envy.template: %s", err.what());
  }

  lua["envy"] = envy_table;

  // ENVY_SHELL table with light userdata constants
  auto shell_table = lua.create_table();
  shell_table["BASH"] = sol::make_light(shell_choice::bash);
  shell_table["SH"] = sol::make_light(shell_choice::sh);
  shell_table["CMD"] = sol::make_light(shell_choice::cmd);
  shell_table["POWERSHELL"] = sol::make_light(shell_choice::powershell);
  lua["ENVY_SHELL"] = shell_table;
}

}  // namespace envy
```

### Direct Sol2 Migration Patterns

**State Creation:**
```cpp
// Before: auto lua_state{ lua_make() }; lua_add_envy(lua_state);
// After:
sol::state lua;
lua.open_libraries(sol::lib::base, sol::lib::package, sol::lib::coroutine,
                   sol::lib::string, sol::lib::os, sol::lib::math,
                   sol::lib::table, sol::lib::debug, sol::lib::bit32,
                   sol::lib::io);
lua_envy_install(lua);
```

**Note:** No `sol::lib::jit` or `sol::lib::ffi` — those are LuaJIT-only. Envy uses standard Lua 5.4.

**Script Execution:**
```cpp
// Before: lua_run_file(lua_state, script_path);
// After:  lua.safe_script_file(script_path.string(), sol::script_pass_on_error);
```

**Global Access:**
```cpp
// Before: std::string id = lua_global_to_string(lua, "identity");
// After:  std::string id = lua["identity"].get<std::string>();
```

**Array Iteration:**
```cpp
// Before: auto packages = lua_global_to_array(lua_state.get(), "packages");
//         for (auto const &pkg : *packages) { /* pkg is lua_value */ }
// After:
sol::table packages = lua["packages"];
for (size_t i = 1; i <= packages.size(); ++i) {
  sol::object pkg = packages[i];
  // Use pkg directly
}
```

### Validation
- Build succeeds: `./build.sh`
- Envy globals installed correctly
- All manifest/recipe code compiles with direct sol2 usage

---

## Phase 3: Update recipe.h + manifest.h State Storage

**Objective:** Rename `lua_state` → `lua` fields for consistency.

**Transformations:**
- `recipe.h`: `sol::state lua_state` → `sol::state lua`
- `manifest.h`: `sol::state lua_state_` → `sol::state lua_`
- Update all references in manifest.cpp, phase_*.cpp files

---

## Phase 4: Migrate recipe_spec.cpp (lua_value → sol::object)

**Objective:** Replace `lua_value` in recipe options with `sol::object`.

**Key changes:**
- `recipe_spec.h`: Change `std::unordered_map<std::string, lua_value> options` → `sol::object`
- `recipe_spec.cpp`: Replace variant checking with sol2 type queries
- Table iteration uses `tbl["key"]` instead of `find()`

---

## Phase 5: Migrate lua_ctx Bindings (Closures → Lambdas)

**Objective:** Replace C closures with lambda captures; eliminate upvalue retrieval.

**Pattern:**
```cpp
// Before: lua_pushcclosure(lua, lua_ctx_run, 1);
// After:  ctx_table["run"] = [context](std::string_view script, sol::optional<sol::table> opts) { ... };
```

**Benefits:**
- No manual upvalue management
- Automatic type conversion for arguments
- Exception-safe error handling

**Files:** lua_ctx_bindings.cpp, lua_ctx_run.cpp, lua_ctx_asset.cpp, lua_ctx_fetch.cpp, etc.

---

## Phase 6: Migrate Phase Execution (Verb Calls)

**Objective:** Replace `lua_getglobal()` + `lua_pcall()` with sol2 protected functions.

**Pattern:**
```cpp
// Before: lua_getglobal(lua, "fetch"); lua_pcall(lua, 1, 1, 0);
// After:
sol::object fetch_obj = r->lua["fetch"];
sol::protected_function fetch_func = fetch_obj.as<sol::protected_function>();
sol::protected_function_result result = fetch_func(ctx_table);
if (!result.valid()) {
  sol::error err = result;
  throw std::runtime_error("Fetch failed: " + std::string{err.what()});
}
```

**Files:** phase_recipe_fetch.cpp, phase_fetch.cpp, phase_check.cpp, phase_install.cpp, manifest.cpp

---

## Phase 7: Delete lua_value and Cleanup

**Objective:** Remove all `lua_value` variant infrastructure.

**Files Deleted:** All `lua_value`, `lua_table`, `lua_variant` types from lua_util.h

**Final State:** lua_util.h contains only helper wrappers if needed; most code uses sol2 directly.

---

## Completion Criteria

- ✅ Sol2 amalgamation generated and cached
- ✅ All raw Lua C API calls replaced
- ✅ `lua_value` variant removed
- ✅ `sol::state` used directly in recipe/manifest
- ✅ All tests pass (unit + functional)
- ✅ Binary size < 5% increase
- ✅ Zero behavioral changes
- ✅ `docs/dependencies.md` updated

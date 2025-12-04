# Enriched Lua Error Messages with Recipe Context

## Problem
When recipes use `assert()` or encounter Lua errors, messages lack critical context:
- No recipe file path or line numbers
- Missing recipe identity and options
- No provenance (which manifest/recipe declared this spec)

Current: `Recipe thread failed: lua: error: local.python@r0.lua:12: attempt to concatenate a nil value`

Target: Show recipe file path, identity with options, declaring file, and provenance chain.

## Solution Architecture

### 1. Path Tracking

**recipe_spec** (compile-time provenance):
```cpp
// src/recipe_spec.h
std::filesystem::path declaring_file_path;  // Manifest or parent recipe that declared this spec
```

**recipe** (runtime information):
```cpp
// src/recipe.h
std::optional<std::filesystem::path> recipe_file_path;  // Actual recipe.lua loaded
```

**Rationale:** `declaring_file_path` is immutable provenance metadata (belongs in spec); `recipe_file_path` is runtime information available only after fetch completes (belongs in recipe).

### 2. Common Lua Function Runner

Create helper in `src/lua_util.h/cpp`:
```cpp
template<typename... Args>
sol::object call_lua_function_with_error_context(
    sol::protected_function func,
    recipe const *r,
    std::string_view phase_name,
    Args&&... args);
```

Replaces manual error handling at all phase sites. Phases become:
```cpp
auto result = call_lua_function_with_error_context(
    build_func, r, "build", ctx_table, opts);
```

All error formatting centralized in helper—no phase-level duplication.

### 3. Centralized Error Formatter

Create `src/lua_error_formatter.h/cpp`:
```cpp
struct lua_error_context {
  std::string lua_error_message;        // From sol::error::what()
  recipe const *r;                      // For all context (spec, paths)
  std::string_view phase;               // "fetch", "build", etc.
};

std::string format_lua_error(lua_error_context const &ctx);
```

Formatter walks `r->spec->parent` chain for provenance, parses Lua stack traces for line numbers, and generates multi-line output.

### 4. Integration Points

**Common runner** (lua_util.cpp): Wraps all `protected_function` calls with error context formatting.

**Engine catch-all** (engine.cpp:535-555): Enriches any errors that escape phases.

**Manifest errors** (manifest.cpp): Simplified format since no recipe context available.

## Implementation Tasks

1. Add `declaring_file_path` field to `recipe_spec` struct (src/recipe_spec.h)
2. Update `recipe_spec` constructor signature to accept `declaring_file_path` parameter (src/recipe_spec.cpp:155-171)
3. Update `recipe_spec::parse()` to pass `base_path` as `declaring_file_path` to constructor (src/recipe_spec.cpp:~330)
4. Add `recipe_file_path` field to `recipe` struct (src/recipe.h)
5. Set `recipe_file_path` in `run_recipe_fetch_phase()` after fetch completes (src/phases/phase_recipe_fetch.cpp:432)
6. Create `src/lua_error_formatter.h` with `lua_error_context` struct and `format_lua_error()` function signature
7. Implement `src/lua_error_formatter.cpp` with line number extraction, provenance chain walker, pretty-printing
8. Add unit tests in `src/lua_error_formatter_tests.cpp`
9. Add `call_lua_function_with_error_context()` template to `src/lua_util.h`
10. Implement common Lua runner in `src/lua_util.cpp` (catches errors, calls formatter, throws enriched error)
11. Replace manual error handling in phase_fetch.cpp:136 with common runner
12. Replace manual error handling in phase_check.cpp:62 with common runner
13. Replace manual error handling in phase_stage.cpp:123 with common runner
14. Replace manual error handling in phase_build.cpp:64 with common runner
15. Replace manual error handling in phase_install.cpp:91 with common runner
16. Replace manual error handling in phase_recipe_fetch.cpp:51 (load script) with common runner
17. Replace manual error handling in phase_recipe_fetch.cpp:248 (products function) with common runner
18. Replace manual error handling in phase_recipe_fetch.cpp:201 (fetch function) with common runner
19. Update engine catch-all (src/engine.cpp:535-555) to enrich any escaping errors
20. Update manifest error handling (src/manifest.cpp:64-70, 125-130) with simplified format
21. Add functional tests with intentional errors at different phases
22. Verify line numbers accurate for local/remote/git sources
23. Confirm provenance chain displays for nested dependencies
24. Test with examples/local.python@r0.lua missing options assertion

## Critical Files

**Core infrastructure:**
- **src/recipe_spec.h** — Add `declaring_file_path` field
- **src/recipe_spec.cpp** — Store `base_path` in constructor (lines 155-171, 330)
- **src/recipe.h** — Add `recipe_file_path` field
- **src/phases/phase_recipe_fetch.cpp** — Set `recipe_file_path` after fetch (line 432)

**Error handling:**
- **src/lua_error_formatter.h/cpp** (NEW) — Core formatting logic
- **src/lua_util.h/cpp** — Common Lua runner helper
- **src/engine.cpp** — Enrich catch-all (lines 535-555)
- **src/phases/phase_*.cpp** (6 files) — Replace manual error handling with common runner

## Design Rationale

**Why recipe_file_path in recipe not recipe_spec?**
- Recipe file unknown until after fetch completes
- Recipe created before fetch happens
- Avoid const_cast by storing in mutable recipe struct

**Why declaring_file_path in recipe_spec?**
- Provenance is immutable metadata
- Known at parse time (base_path parameter)
- Belongs with parent pointer for ownership tracking

**Why common Lua runner?**
- DRY across 6+ phase sites
- Consistent error handling everywhere
- Single place to update formatting logic

**Why template for runner?**
- Type-safe: preserves return types and argument forwarding
- Zero overhead: inlines at compile time
- Flexible: handles any Lua function signature

## Example Output

```
Lua error in local.python@r0{}:
  local.python@r0.lua:12: assertion failed: version option is required
  stack traceback:
    local.python@r0.lua:12: in function 'products'

Recipe file: /Users/user/src/envy/examples/local.python@r0.lua:12
Declared in: /Users/user/src/envy/examples/local.ninja@r0.lua (dependencies table)
Phase: recipe_fetch
Options: {}
```

## Future Enhancements

- Colored output for TTY (highlight file paths, line numbers)
- Clickable file paths (OSC 8 hyperlinks)
- JSON error format for machine consumption
- Integration with Lua `debug.traceback()` for deeper stacks

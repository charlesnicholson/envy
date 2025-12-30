# Lua API Ergonomics Redesign

## Problem Statement

Current `ctx` table design has significant ergonomics and discoverability problems:

1. **Opaque API surface** - No way to discover available methods without reading C++ source
2. **Overloaded `run_dir`** - Means `tmp_dir` (fetch), `stage_dir` (build), or `project_root` (check) depending on phase
3. **Implicit path anchoring** - Relative paths auto-resolve to hidden `run_dir` with no visible indication
4. **Poor error messages** - Report `"source.txt"` not found, not resolved `"run_dir/source.txt"`
5. **No IDE support** - lua_ls cannot provide autocomplete or type checking for opaque ctx table
6. **String concatenation paths** - `ctx.install_dir .. "/bin"` is error-prone and platform-dependent
7. **Late-bound restrictions** - User-managed packages throw runtime errors for forbidden methods

## Design Solution

Eliminate `ctx` table entirely. Replace with:
1. **Explicit directory parameters** - Phase functions receive directories as named parameters
2. **Global `envy.*` namespace** - All operations grouped under discoverable namespace
3. **Platform-aware path utilities** - `envy.path.join()` eliminates string concatenation errors
4. **lua_ls type definitions** - Enable IDE autocomplete and inline documentation
5. **Sensible defaults** - `envy.run()` defaults cwd to phase-appropriate directory

## New API Design

### Phase Function Signatures

All phase verbs receive explicit directory parameters. tmp_dir available to all phases (scoped_entry_lock always creates it); install_dir is nil for user-managed packages.

```lua
-- Fetch phase: download source materials to private tmp_dir
-- Default cwd for envy.run(): tmp_dir
-- NOTE: fetch_dir is NOT exposed—use envy.commit_fetch() to atomically move verified files
FETCH = function(tmp_dir)
  local files = envy.fetch("https://example.com/lib.tar.gz", {dest = tmp_dir})
  envy.commit_fetch(files)  -- Atomically moves verified files to fetch_dir
end

-- Stage phase: extract and prepare (fetch_dir now readable)
-- Default cwd for envy.run(): stage_dir
STAGE = function(fetch_dir, stage_dir, tmp_dir)
  envy.extract_all(fetch_dir, stage_dir, {strip = 1})
end

-- Build phase: compile artifacts (fetch_dir available for reference)
-- Default cwd for envy.run(): stage_dir
-- NOTE: install_dir is NOT exposed to BUILD—use INSTALL phase for final artifacts
BUILD = function(stage_dir, fetch_dir, tmp_dir)
  envy.run("./configure")  -- cwd defaults to stage_dir
  envy.run("make")
end

-- Install phase: uniform signature for both cache-managed and user-managed
-- Default cwd for envy.run(): install_dir (cache-managed) or project_root (user-managed)
-- install_dir is nil for user-managed packages
INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir)
  if install_dir then
    -- Cache-managed: write artifacts to install_dir
    envy.copy(envy.path.join(stage_dir, "extras"), envy.path.join(install_dir, "extras"))
  else
    -- User-managed: install_dir is nil, modify system directly
    envy.run("brew install python3")
  end
end

-- Check phase: test if package already satisfied (no lock yet, no tmp_dir)
-- Default cwd for envy.run(): project_root
CHECK = function(project_root)
  local result = envy.run("python3 --version", {capture = true, quiet = true})
  return result.exit_code == 0
end
```

### Declarative Shortcuts Preserved

Flexibility for common patterns maintained:

```lua
-- Fetch declarative forms (all valid)
FETCH = "https://example.com/lib.tar.gz"
FETCH = {url = "https://example.com/lib.tar.gz", sha256 = "abc123..."}
FETCH = {{url = "..."}, {url = "..."}}
FETCH = function(tmp_dir) ... end  -- Custom fetch receives only tmp_dir

-- Stage declarative shorthand
STAGE = {strip = 1}  -- Expands to extract_all with strip
STAGE = function(fetch_dir, stage_dir, tmp_dir) ... end

-- Build/Install declarative shortcuts
BUILD = "make -j"
INSTALL = "make install"
BUILD = function(...) ... end
INSTALL = function(...) ... end
```

### Global envy.* Namespace

All operations moved to global namespace:

```lua
-- File operations (explicit paths, no implicit anchoring)
envy.copy(src_path, dst_path)
envy.move(src_path, dst_path)
envy.remove(path)
envy.exists(path) → boolean
envy.is_file(path) → boolean
envy.is_dir(path) → boolean

-- Path utilities (platform-aware, eliminates string concatenation)
envy.path.join(part1, part2, ...) → string
envy.path.basename(path) → string
envy.path.dirname(path) → string
envy.path.stem(path) → string
envy.path.extension(path) → string

-- Process execution (explicit cwd with sensible defaults per phase)
envy.run(script, opts?) → {exit_code: number, stdout?: string, stderr?: string}
  -- script: string or string[] (array of commands)
  -- opts: {cwd?: string, env?: table, quiet?: boolean, capture?: boolean, check?: boolean, interactive?: boolean}
  -- cwd defaults: tmp_dir (FETCH), stage_dir (STAGE/BUILD), install_dir (INSTALL cache-managed), tmp_dir (INSTALL user-managed), project_root (CHECK)

-- Archive extraction (explicit destination)
envy.extract(archive_path, dest_dir, opts?) → file_count
  -- opts: {strip?: number}
envy.extract_all(src_dir, dest_dir, opts?)
  -- opts: {strip?: number}

-- Download operations (explicit destination)
envy.fetch(url_or_spec, opts) → string | string[]
  -- url_or_spec: string | {url: string, sha256?: string} | array of tables
  -- opts: {dest: string} REQUIRED
  -- Returns: basename string (single file) or array of basenames (multiple files)
envy.commit_fetch(files)
  -- files: string | string[] — basenames downloaded to tmp_dir
  -- Atomically moves verified files from tmp_dir to fetch_dir
  -- Only callable from FETCH phase; fetch_dir never directly exposed
envy.verify_hash(file_path, expected_sha256) → boolean

-- Dependency access (uses thread-local engine/recipe context)
envy.asset(identity) → path_string
envy.product(name) → path_or_value_string

-- Logging (forwards to tui::*)
envy.log(message)
envy.warn(message)
envy.error(message)
envy.debug(message)

-- Platform globals (move from root globals to envy namespace)
envy.PLATFORM → "darwin" | "linux" | "windows"
envy.ARCH → "arm64" | "x86_64" | etc.
envy.PLATFORM_ARCH → "darwin-arm64" | etc.
```

### Identity Access

Recipes already declare `IDENTITY` as global variable—no need to pass as parameter:

```lua
IDENTITY = "mylib@v1"

FETCH = function(tmp_dir, fetch_dir)
  envy.log("Fetching " .. IDENTITY)  -- Use global IDENTITY
end
```

## lua_ls Integration

### Type Definitions

Provide `.envy/types/envy.lua` with complete API annotations:

```lua
---@meta

---Global recipe identity (required in all recipe files)
---@type string
IDENTITY = ""

---Envy core namespace
---@class envy
envy = {}

---Path manipulation utilities
---@class envy.path
envy.path = {}

---Join path components with platform-appropriate separator
---@param ... string Path components to join
---@return string Joined absolute path
function envy.path.join(...) end

-- ... (complete API annotated)

---Fetch phase: download source materials to private tmp_dir
---@param tmp_dir string Temporary download directory (default cwd for envy.run)
---Use envy.commit_fetch() to move verified files to fetch_dir (not directly exposed)
function FETCH(tmp_dir) end

---Stage phase: extract and prepare sources
---@param fetch_dir string Directory containing committed fetch artifacts
---@param stage_dir string Extraction destination (default cwd for envy.run)
---@param tmp_dir string Temporary scratch directory
function STAGE(fetch_dir, stage_dir, tmp_dir) end

---Build phase: compile artifacts
---@param stage_dir string Extracted sources directory (default cwd for envy.run)
---@param fetch_dir string Downloaded archives directory (rarely needed)
---@param tmp_dir string Temporary scratch directory
function BUILD(stage_dir, fetch_dir, tmp_dir) end

---Install phase: final artifact preparation (cache-managed) or system modification (user-managed)
---@param install_dir string|nil Installation directory (nil for user-managed packages)
---@param stage_dir string Extracted sources directory
---@param fetch_dir string Downloaded archives directory
---@param tmp_dir string Temporary scratch directory
function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir) end

-- ... (all phase signatures)
```

### Workspace Configuration

Projects include `.luarc.json`:

```json
{
  "runtime.version": "Lua 5.4",
  "workspace.library": [".envy/types"],
  "diagnostics.globals": ["IDENTITY", "PACKAGES", "FETCH", "STAGE", "BUILD", "INSTALL", "CHECK", "DEPENDENCIES", "PRODUCTS", "envy"]
}
```

### IDE Experience

With lua_ls configured:
- **Autocomplete**: Type `envy.` → see all available methods
- **Signature help**: Type `envy.run(` → see parameter info and descriptions
- **Type checking**: `envy.path.join(123)` → warning "expected string, got number"
- **Hover docs**: Cursor over `envy.run` → see full signature with parameter descriptions
- **Go to definition**: Jump to type definition for API reference

### Type Checking Limitations

Declarative forms cannot be perfectly type-checked:

```lua
---@type string|{url: string, sha256?: string}|fun(tmp_dir: string, fetch_dir: string)|nil
FETCH = nil
```

lua_ls will accept any form but cannot validate function signatures when user writes `FETCH = function(...)`. This is acceptable trade-off—declarative ergonomics take priority over perfect type checking. Users still benefit from autocomplete inside function bodies and hover documentation showing canonical signatures.

## Thread-Local Context Analysis

`envy.asset()` and `envy.product()` require access to current engine and recipe pointers for dependency validation. Current ctx design passes these explicitly; new design uses thread-local storage.

### Thread Safety Analysis

**Current architecture:**
- Each recipe executes on its own std::thread (one thread per recipe)
- Multiple recipes execute in parallel on different threads
- Single recipe's phase execution is sequential (no coroutines, no async callbacks)
- Each recipe has isolated lua_state (no shared Lua state between recipes)

**Thread-local approach:**
```cpp
// src/lua_ctx/lua_phase_context.h
namespace envy {
  // Thread-local storage
  thread_local engine* g_current_engine = nullptr;
  thread_local recipe* g_current_recipe = nullptr;

  // RAII guard sets context for current thread
  struct phase_context_guard {
    phase_context_guard(engine* eng, recipe* r) {
      g_current_engine = eng;
      g_current_recipe = r;
    }
    ~phase_context_guard() {
      g_current_engine = nullptr;
      g_current_recipe = nullptr;
    }
  };

  engine* get_current_engine() { return g_current_engine; }
  recipe* get_current_recipe() { return g_current_recipe; }
}

// Usage in phase execution
{
  phase_context_guard guard(&eng, r);
  lua.safe_script_file(recipe_path);  // envy.asset() can access context
}
```

**Safety properties:**
1. **Thread isolation**: Each worker thread has separate thread-local storage—no cross-thread interference
2. **RAII cleanup**: Guard destructor clears context even on exception—no stale state
3. **Sequential execution**: Recipe phases execute sequentially on single thread—no re-entrancy
4. **No async callbacks**: All envy.* functions are synchronous (envy.run() blocks until complete)—thread-local always valid during Lua execution

**Potential race conditions (all safe):**
- **Scenario A**: Recipe A on thread 1, recipe B on thread 2 execute concurrently
  - **Analysis**: Each thread has separate thread-local storage—no shared state, no race
- **Scenario B**: Recipe A calls envy.asset() which triggers recipe B's execution on different thread
  - **Analysis**: Recipe B gets its own phase_context_guard on its thread—isolated contexts, no race
- **Scenario C**: Exception during phase execution
  - **Analysis**: RAII destructor runs during stack unwinding—context always cleared

**Conclusion**: Thread-local approach is safe. Each thread's context is isolated, RAII ensures cleanup, and Lua execution is synchronous with no async callbacks that could access wrong thread's context.

## User-Managed Packages and Temporary Directories

User-managed packages (CHECK verb present) modify system state rather than producing cached artifacts. They can use the full FETCH/STAGE/BUILD/INSTALL pipeline; `scoped_entry_lock` purges the entire workspace upon completion (cache.cpp:181-184).

**Key insight:** `scoped_entry_lock` always creates `tmp_dir` (cache.cpp:154). Cleanup is automatic via destructor. Since tmp_dir always exists, pass it to all phases.

### Chosen Design

**Uniform signature with nil install_dir + tmp_dir to all phases:**

```lua
FETCH   = function(tmp_dir) ... end
STAGE   = function(fetch_dir, stage_dir, tmp_dir) ... end
BUILD   = function(stage_dir, fetch_dir, tmp_dir) ... end  -- install_dir NOT exposed
INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir) ... end  -- install_dir=nil for user-managed
CHECK   = function(project_root) ... end  -- no tmp_dir (lock not yet acquired)
```

**Properties:**
- Consistent signatures regardless of cache-managed vs user-managed
- nil install_dir signals user-managed context
- tmp_dir available to FETCH, STAGE, BUILD, INSTALL for scratch work
- Full pipeline available to user-managed packages
- User-managed default cwd: project_root (where manifest lives)
- Existing scoped_entry_lock cleanup handles all cases

**Example: User-managed package with full pipeline**

```lua
IDENTITY = "custom.tool@v1"

CHECK = function(project_root)
  return envy.exists(envy.path.join(os.getenv("HOME"), ".local/bin/tool"))
end

FETCH = function(tmp_dir)
  envy.fetch({url = "https://example.com/tool.tar.gz", sha256 = "abc..."}, {dest = tmp_dir})
  envy.commit_fetch("tool.tar.gz")
end

STAGE = function(fetch_dir, stage_dir, tmp_dir)
  envy.extract_all(fetch_dir, stage_dir, {strip = 1})
end

INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir)
  -- install_dir is nil for user-managed
  envy.run("cp " .. envy.path.join(stage_dir, "bin/tool") .. " ~/.local/bin/")
end
-- On completion: scoped_entry_lock destructor purges entire entry_dir
```

## Declarative Expansion Strategy

Declarative forms expand in C++ phase code (not Lua preprocessing). This handles complex cases like:

```lua
BUILD = "make -j"  -- Simple string
BUILD = {"cmake --build .", "cmake --install ."}  -- Array of commands
BUILD = envy.template("{{tool}} -j {{threads}}", tool = "make", threads = 8)  -- Template (future)
```

Expansion happens in phase_*.cpp before calling Lua function. For example:

```cpp
// phase_build.cpp
sol::object build_obj = lua["BUILD"];
if (build_obj.is<std::string>()) {
  // Declarative string: expand to envy.run(cmd, {cwd = stage_dir})
  std::string cmd = build_obj.as<std::string>();
  sol::table env = lua.create_table();
  env["stage_dir"] = stage_dir.string();
  env["install_dir"] = install_dir.string();
  env["fetch_dir"] = fetch_dir.string();
  lua.safe_script("envy.run([[" + cmd + "]], {cwd = stage_dir})");
} else if (build_obj.is<sol::function>()) {
  // Function form: call directly
  sol::protected_function build_fn = build_obj;
  build_fn(stage_dir.string(), install_dir.string(), fetch_dir.string());
}
```

## Existing lua_envy Infrastructure

`src/lua_envy.cpp` already implements foundational envy namespace registration via `lua_envy_install()`:

**Already complete:**
- `envy` table creation
- Logging functions: `envy.trace()`, `envy.debug()`, `envy.info()`, `envy.warn()`, `envy.error()`, `envy.stdout()`
- Template function: `envy.template(str, values)` - string interpolation with `{{placeholder}}` syntax
- Platform globals: `ENVY_PLATFORM`, `ENVY_ARCH`, `ENVY_PLATFORM_ARCH`, `ENVY_EXE_EXT` (currently at root level)
- Shell constants: `ENVY_SHELL.BASH`, `ENVY_SHELL.SH`, `ENVY_SHELL.CMD`, `ENVY_SHELL.POWERSHELL` (root level)
- `print()` override routing to `tui::info()`

## File Organization

**`src/lua_envy.cpp`** - Main coordinator
- Has: logging, template, print override, platform globals, ENVY_SHELL
- Adds: calls all module `*_install()` functions
- Entry point: `lua_envy_install(sol::state&)`

**`src/lua_ctx/lua_envy_path.cpp`** - Path utilities
- Functions: `envy.path.join/basename/dirname/stem/extension()`
- Exports: `lua_envy_path_install(sol::table& envy_table)`

**`src/lua_ctx/lua_envy_file_ops.cpp`** - File operations
- Functions: `envy.copy/move/remove/exists/is_file/is_dir()`
- Exports: `lua_envy_file_ops_install(sol::table& envy_table)`

**`src/lua_ctx/lua_envy_run.cpp`** - Process execution
- Functions: `envy.run()`
- Exports: `lua_envy_run_install(sol::table& envy_table)`

**`src/lua_ctx/lua_envy_extract.cpp`** - Archive extraction
- Functions: `envy.extract/extract_all()`
- Exports: `lua_envy_extract_install(sol::table& envy_table)`

**`src/lua_ctx/lua_envy_fetch.cpp`** - Download operations
- Functions: `envy.fetch/commit_fetch/verify_hash()`
- Exports: `lua_envy_fetch_install(sol::table& envy_table)`

**`src/lua_ctx/lua_envy_deps.cpp`** - Dependency access
- Functions: `envy.asset/product()`
- Exports: `lua_envy_deps_install(sol::table& envy_table)`

**`src/lua_ctx/lua_phase_context.{h,cpp}`** - Thread-local context
- RAII guard + getters for engine/recipe pointers
- Used by asset/product for dependency validation

## Implementation Tasks

### Infrastructure Setup

- [x] Update `src/lua_envy.cpp` - Move platform globals from root to `envy.PLATFORM`, `envy.ARCH`, `envy.PLATFORM_ARCH`, `envy.EXE_EXT`
- [x] Update `src/lua_envy.cpp` - Keep logging functions as-is (already complete)
- [x] Update `src/lua_envy.cpp` - Keep `envy.template()` as-is (already complete)
- [x] Update `src/lua_envy.cpp` - Keep `print()` override as-is (already complete)
- [x] Update `src/lua_envy.cpp` - Keep `ENVY_SHELL` constants at root level (used by manifest `DEFAULT_SHELL` feature)
- [x] Create `src/lua_ctx/lua_envy_path.cpp` - Path utility implementations, register with `lua_envy_path_install(envy_table)`
- [x] Create `src/lua_ctx/lua_phase_context.h` - Thread-local context storage declarations
- [x] Create `src/lua_ctx/lua_phase_context.cpp` - Thread-local context implementation with RAII guard
- [x] Update `src/lua_envy.cpp` - Call all module registration functions from `lua_envy_install()`
- [x] Update `CMakeLists.txt` - Add new source files to build

### Path Utilities Implementation

- [x] Create `src/lua_ctx/lua_envy_path.cpp` with registration function `lua_envy_path_install(sol::table& envy_table)`
- [x] Implement `envy.path.join()` - Variadic path joining using std::filesystem::path operator/
- [x] Implement `envy.path.basename()` - Extract filename with extension
- [x] Implement `envy.path.dirname()` - Extract parent directory path
- [x] Implement `envy.path.stem()` - Extract filename without extension
- [x] Implement `envy.path.extension()` - Extract file extension with leading dot
- [x] Register all path utilities in envy.path sub-table via `lua_envy_path_install()`
- [x] Update `src/lua_envy.cpp` to call `lua_envy_path_install(envy_table)` from `lua_envy_install()`

### Platform Globals Migration

- [x] Update `src/lua_envy.cpp` - Move `ENVY_PLATFORM` from `lua["ENVY_PLATFORM"]` to `envy_table["PLATFORM"]`
- [x] Update `src/lua_envy.cpp` - Move `ENVY_ARCH` from `lua["ENVY_ARCH"]` to `envy_table["ARCH"]`
- [x] Update `src/lua_envy.cpp` - Move `ENVY_PLATFORM_ARCH` from `lua["ENVY_PLATFORM_ARCH"]` to `envy_table["PLATFORM_ARCH"]`
- [x] Update `src/lua_envy.cpp` - Move `ENVY_EXE_EXT` from `lua["ENVY_EXE_EXT"]` to `envy_table["EXE_EXT"]`
- [x] Update all test recipes using old root-level global names (ENVY_PLATFORM → envy.PLATFORM, etc.)

### File Operations Migration

- [x] Create `src/lua_ctx/lua_envy_file_ops.cpp` with registration function `lua_envy_file_ops_install(sol::table& envy_table)`
- [x] Implement `envy.copy(src, dst)` - Copy file or directory (no implicit anchoring, all paths absolute)
- [x] Implement `envy.move(src, dst)` - Move/rename file or directory
- [x] Implement `envy.remove(path)` - Delete file or directory recursively
- [x] Implement `envy.exists(path)` - Check if path exists
- [x] Implement `envy.is_file(path)` - Check if path is regular file
- [x] Implement `envy.is_dir(path)` - Check if path is directory
- [x] Register file operations via `lua_envy_file_ops_install()`
- [x] Update `src/lua_envy.cpp` to call `lua_envy_file_ops_install(envy_table)` from `lua_envy_install()`
- [x] Update error messages to show actual resolved paths (not relative)

### Process Execution Migration

- [x] Create `src/lua_ctx/lua_envy_run.cpp` with registration function `lua_envy_run_install(sol::table& envy_table)`
- [x] Implement `envy.run(script, opts?)` with optional cwd (no implicit default yet)
- [x] Add default cwd logic per phase: tmp_dir (FETCH), stage_dir (STAGE/BUILD), install_dir (INSTALL cache-managed), project_root (INSTALL user-managed), project_root (CHECK)
- [x] Support script as string or string[] (array of commands)
- [x] Support opts table: {cwd, env, quiet, capture, check, interactive}
- [x] Return table: {exit_code, stdout?, stderr?}
- [x] Register envy.run via `lua_envy_run_install()`
- [x] Update `src/lua_envy.cpp` to call `lua_envy_run_install(envy_table)` from `lua_envy_install()`

### Archive Operations Migration

- [x] Create `src/lua_ctx/lua_envy_extract.cpp` with registration function `lua_envy_extract_install(sol::table& envy_table)`
- [x] Implement `envy.extract(archive_path, dest_dir, opts?)` - Single archive extraction with explicit destination
- [x] Implement `envy.extract_all(src_dir, dest_dir, opts?)` - Extract all archives in src_dir to dest_dir
- [x] Support opts table: {strip} for strip_components
- [x] Register extraction functions via `lua_envy_extract_install()`
- [x] Update `src/lua_envy.cpp` to call `lua_envy_extract_install(envy_table)` from `lua_envy_install()`

### Fetch Operations Migration

- [x] Create `src/lua_ctx/lua_envy_fetch.cpp` with registration function `lua_envy_fetch_install(sol::table& envy_table)`
- [x] Implement `envy.fetch(url_or_spec, opts)` with required `dest` in opts
- [x] Support url_or_spec as string, table {url, sha256?}, or array of tables
- [x] Return basename string (single file) or array of basenames (multiple files)
- [x] Implement `envy.commit_fetch(files)` - atomically move verified files from tmp_dir to fetch_dir
- [x] commit_fetch only callable from FETCH phase (throw error in other phases)
- [x] Implement `envy.verify_hash(file_path, sha256)` - SHA256 verification utility
- [x] Register fetch operations via `lua_envy_fetch_install()`
- [x] Update `src/lua_envy.cpp` to call `lua_envy_fetch_install(envy_table)` from `lua_envy_install()`

### Dependency Access Migration

- [x] Create `src/lua_ctx/lua_envy_deps.cpp` with registration function `lua_envy_deps_install(sol::table& envy_table)`
- [x] Implement `envy.asset(identity)` using thread-local context (get_current_engine/recipe)
- [x] Implement `envy.product(name)` using thread-local context
- [x] Register dependency functions via `lua_envy_deps_install()`
- [x] Update `src/lua_envy.cpp` to call `lua_envy_deps_install(envy_table)` from `lua_envy_install()`
- [x] Validate thread-local context is set (throw clear error if called outside phase execution)

### Phase Signature Updates - FETCH

- [x] Update `src/phases/phase_fetch.cpp` - FETCH function signature is (tmp_dir, opts)
- [x] fetch_dir never exposed to FETCH function—must use envy.commit_fetch() to move verified files
- [x] Implement envy.commit_fetch(files) - atomically moves basenames from tmp_dir to fetch_dir
- [x] Set thread-local context with phase_context_guard before calling FETCH function
- [x] Handle declarative FETCH forms (string, table, array) with C++ expansion
- [x] Update default cwd for this phase to tmp_dir (if envy.run called without explicit cwd)

### Phase Signature Updates - STAGE

- [x] Update `src/phases/phase_stage.cpp` - Change STAGE function signature to (fetch_dir, stage_dir, tmp_dir)
- [x] Set thread-local context with phase_context_guard before calling STAGE function
- [x] Handle declarative STAGE form {strip = N} with C++ expansion to envy.extract_all
- [x] Update default cwd for this phase to stage_dir

### Phase Signature Updates - BUILD

- [x] Update `src/phases/phase_build.cpp` - Change BUILD function signature to (stage_dir, fetch_dir, tmp_dir)
- [x] install_dir NOT exposed to BUILD—use INSTALL phase for final artifacts
- [x] Set thread-local context with phase_context_guard before calling BUILD function
- [x] Handle declarative BUILD form (string or array) with C++ expansion to envy.run
- [x] Update default cwd for this phase to stage_dir

### Phase Signature Updates - INSTALL

- [x] Update `src/phases/phase_install.cpp` - Unified INSTALL signature: (install_dir, stage_dir, fetch_dir, tmp_dir)
- [x] Pass nil for install_dir when user-managed (CHECK present)
- [x] Set thread-local context with phase_context_guard before calling INSTALL function
- [x] Handle declarative INSTALL form (string or array) with C++ expansion to envy.run
- [x] Update default cwd for cache-managed to install_dir
- [x] Update default cwd for user-managed to project_root
- [x] Remove old forbidden method logic (no longer needed—nil install_dir signals user-managed)

### Phase Signature Updates - CHECK

- [x] Update `src/phases/phase_check.cpp` - Change CHECK function signature to (project_root)
- [x] Set thread-local context with phase_context_guard before calling CHECK function
- [x] Update default cwd for this phase to project_root
- [x] Handle CHECK return value (boolean or string command)

### User-Managed Packages Full Pipeline

- [x] User-managed packages (CHECK present) CANNOT define FETCH/STAGE/BUILD verbs (enforced in phase_recipe_fetch.cpp)
- [x] INSTALL receives (install_dir, stage_dir, fetch_dir, tmp_dir) for all packages
- [x] install_dir = nil for user-managed packages (signaling not applicable)
- [x] tmp_dir available to INSTALL phase for user-managed (via lock)
- [x] Existing scoped_entry_lock with user_managed_ flag already purges workspace on completion
- [x] Update architecture.md to clarify user-managed package restrictions
- [x] Add functional test for user-managed package with CHECK+INSTALL (existing in test_user_managed.py)

### Remove Old ctx Implementation

- [x] Remove `src/lua_ctx/lua_ctx_bindings.h` build_*_ctx_table functions (no longer used by phases)
- [x] Remove old ctx construction code from all phase_*.cpp files
- [x] Keep `src/lua_ctx/lua_ctx_common` struct (still needed for DEFAULT_SHELL function ctx:asset() calls in manifest.cpp)
- [x] Clean up unused includes and forward declarations from phase files

### Test Recipe Migration

- [x] Audit all recipes in `test_data/specs/*.lua` - identify patterns
- [x] Create regex migration script `scripts/migrate_recipes.py` for common patterns
- [x] Run migration script on test recipes
- [x] Build envy and run functional tests - identify failures
- [x] Manually fix recipes that script couldn't handle
- [x] Verify all functional tests pass
- [x] Review migrated recipes for clarity and best practices

### lua_ls Type Definitions

- [ ] Create `.envy/types/envy.lua` with @meta annotation
- [ ] Document envy.path namespace with all functions
- [ ] Document envy file operations (copy, move, remove, exists, is_file, is_dir)
- [ ] Document envy.run with complete opts table structure
- [ ] Document envy.extract and envy.extract_all with opts
- [ ] Document envy.fetch with url_or_spec variants
- [ ] Document envy.asset and envy.product
- [ ] Document logging functions (log, warn, error, debug)
- [ ] Document platform globals (PLATFORM, ARCH, PLATFORM_ARCH)
- [ ] Document all phase function signatures (FETCH, STAGE, BUILD, INSTALL, CHECK)
- [ ] Add parameter descriptions with @param annotations
- [ ] Add return type annotations with @return
- [ ] Create `.luarc.json.template` workspace configuration
- [ ] Test type definitions with lua_ls in VSCode/Neovim

### Documentation Updates

- [x] Update `docs/architecture.md` - Rewrite "Recipes" section with new API
- [x] Update `docs/architecture.md` - Remove ctx table documentation
- [x] Create `docs/lua_api.md` - Complete envy.* namespace reference
- [ ] Create `docs/lua_ls_setup.md` - IDE integration guide for users (lua_ls related)
- [ ] Update `README.md` - Add section on IDE support with lua_ls (lua_ls related)
- [x] Document declarative form behavior and C++ expansion strategy (see "Declarative Expansion Strategy" section above)
- [x] Document thread-local context approach and safety analysis (see "Thread-Local Context Analysis" section above)
- [x] Add migration guide for existing recipes (see `docs/lua_api.md` migration table)

### Final Validation

- [x] Run `./build.sh` - Verify clean build with no warnings
- [x] Run all unit tests - Verify 100% pass rate
- [x] Run all functional tests - Verify 100% pass rate
- [ ] Test lua_ls integration in real editor (VSCode or Neovim)
- [ ] Verify autocomplete works for envy.* namespace
- [ ] Verify hover documentation shows for all functions
- [x] Verify declarative forms still work (FETCH/BUILD/INSTALL shortcuts)
- [x] Verify user-managed packages work correctly (CHECK + INSTALL)
- [x] Verify cache-managed packages work correctly (full phase pipeline)
- [x] Spot-check error messages include resolved paths (not relative paths)

# Envy Architecture

## Project Manifests

All script-global variables are uppercase: manifests export `PACKAGES`; recipes declare `IDENTITY`, `FETCH`, `CHECK`, `STAGE`, `BUILD`, `INSTALL`, `DEPENDENCIES`, and `PRODUCTS`.

**Syntax:** Shorthand `"namespace.name@version"` expands to `{ recipe = "namespace.name@version" }`. Table syntax supports `source`, `sha256`, `file`, `fetch`, `options`, `dependencies`, `needed_by`.

**Platform-specific packages:** Manifests are Lua scripts—use conditionals and `envy.join()` to combine common and OS-specific package lists.

```lua
-- project/envy.lua
local common = {
    {  -- Declarative remote with verification and options
        recipe = "arm.gcc@v2",
        source = "https://github.com/arm/recipes/gcc-v2.lua",
        sha256 = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        options = { version = "13.2.0", target = "arm-none-eabi" },
    },
    {  -- Git repository
        recipe = "vendor.openocd@v3",
        source = "git://github.com/vendor/openocd-recipe.git",
        ref = "a1b2c3d4e5f6...",  -- Commit SHA
        options = { target = "arm" },
    },
    {  -- Custom fetch (JFrog example)
        recipe = "corporate.toolchain@v1",
        FETCH = function(ctx)
            local jfrog = ctx:asset("jfrog.cli@v2")
            local tmp = ctx.tmp_dir
            ctx:run(jfrog .. "/bin/jfrog", "rt", "download", "recipes/toolchain.lua", tmp .. "/recipe.lua")
            ctx:commit_fetch({filename = "recipe.lua", sha256 = "sha256_here..."})
        end,
        DEPENDENCIES = {
            { recipe = "jfrog.cli@v2", source = "...", sha256 = "...", needed_by = "recipe_fetch" }
        }
    },
    {  -- Project-local (development)
        recipe = "local.wrapper@v1",
        file = "./envy/recipes/wrapper.lua",
        options = { base = "arm.gcc@v2" },
    },
}

local darwin_packages = {
    "envy.homebrew@v4",
}

local linux_packages = {
    "system.apt@v1",
}

PACKAGES = ENVY_PLATFORM == "darwin" and envy.join(common, darwin_packages)
        or ENVY_PLATFORM == "linux" and envy.join(common, linux_packages)
        or common
```

**Field semantics:**
- `identity` — Recipe identity declaration (**required in all recipe files**, no exemptions)
- `source` — URL (http/https/s3/git/file) or Git repo for declarative fetch
- `ref` — Git commit SHA or committish (required for git sources)
- `sha256` — Expected hash for verification (**optional**, permissive by default; future strict mode will require for non-`local.*`)
- `fetch` — Custom Lua function for exotic sources (JFrog, authenticated APIs); mutually exclusive with `source`
- `file` — Project-local recipe path (never cached; `local.*` namespace only)
- `subdir` — Subdirectory within archive or git repo containing recipe entry point
- `options` — Recipe-specific configuration (passed to recipe Lua as `ctx.options`)
- `dependencies` — Transitive dependencies (recipes this recipe needs)
  - **Strong:** full recipe spec with `source` (or manifest-provided source)
  - **Weak:** partial `recipe` plus `weak = { ... }` fallback spec
  - **Reference-only:** partial `recipe` with no `source`/`weak` (must be satisfied by some other provider)
  - **Nested fetch prerequisites:** inside `source.dependencies`, may also be weak/reference-only
- `needed_by` — Phase dependency annotation (default: `"fetch"`, custom: `"recipe_fetch"`, `"build"`, etc.)

**Uniqueness validation:** Envy validates manifests post-execution. Duplicate recipe+options combinations error (deep comparison—string `"foo@v1"` matches `{ recipe = "foo@v1" }`). Same recipe with conflicting sources (different `source`/`sha256`/`file`/`fetch`) errors. Same recipe+options from identical sources is duplicate. Different options yield different deployments—allowed.

## Shell Configuration

Manifests can specify a `DEFAULT_SHELL` global to control how `ctx:run()` executes scripts across all recipes. This enables portable build scripts in custom languages without requiring pre-installed interpreters.

**Built-in shells (constants):**
- `ENVY_SHELL.BASH` — POSIX bash (default on macOS/Linux)
- `ENVY_SHELL.SH` — POSIX sh
- `ENVY_SHELL.CMD` — Windows cmd.exe
- `ENVY_SHELL.POWERSHELL` — Windows PowerShell (default on Windows)

**Custom shells (table):**
- **File mode:** `{file = "/path/to/interpreter", ext = ".ext"}` or `{file = {"/path/to/exe", "--arg"}, ext = ".tcl"}`
  - Script written to temp file with extension, path passed as final argument
  - Shorthand: `file = "/path"` expands to `file = {"/path"}`
- **Inline mode:** `{inline = {"/path/to/exe", "-c"}}`
  - Script content passed as final argument (no temp file)

**Dynamic shell selection (function):**
```lua
DEFAULT_SHELL = function(ctx)
  -- Query deployed assets to use as interpreter
  local python = ctx:asset("python@v3.11")
  return {inline = {python .. "/bin/python3", "-c"}}
end
```

**Use case:** Express all build scripts in a custom language (Python, Tcl, Ruby) without assuming it's pre-installed. The function can query `ctx:asset()` to locate envy-deployed interpreters. Bootstrap recipes (Python itself) must use built-in shells.

**Implementation notes:**
- Functions evaluated lazily during engine execution (after dependency graph built)
- Result cached per manifest
- **Future work:** Dependency analysis—extract `ctx:asset()` calls from function to add implicit dependencies, ensuring interpreter deploys before dependent recipes run

## Recipes

### Organization

**Identity:** Recipes are namespaced with version: `arm.gcc@v2`, `gnu.binutils@v3`. The `@` symbol denotes **recipe version**, not asset version. Asset versions come from `options` in manifest. Multiple recipe versions coexist; `local.*` namespace reserved for project-local recipes.

**Sources:**
- **Declarative:** `source` field with URL (http/https/s3/file) or git repo; verified via `sha256` (URL) or `ref` (git); cached
- **Custom fetch:** `fetch` function with verification enforced at API boundary (`ctx:fetch`, `ctx:import_file`); cached
- **Project-local:** `file` path in project tree; never cached; `local.*` namespace only
- **Fetch prerequisites (nested):** `source.dependencies` declares recipes that must reach completion before this recipe’s fetch runs. These can be strong, weak, or reference-only.

**Formats:**
- **Single-file:** `.lua` file (declarative sources only)
- **Multi-file:** Directory with `recipe.lua` entry point (custom fetch, archives, git repos)

**Integrity:** Two orthogonal checks:

1. **Identity validation** (ALL recipes, always required):
   - Recipe must declare `identity = "..."` matching referrer's expectation
   - Catches typos, stale references, copy-paste errors
   - No namespace exemptions

2. **SHA256 verification** (optional, namespace-specific):
  - Declarative sources accept SHA256 (URL) or commit SHA (git)
  - Custom fetch accepts SHA256 per-file via `ctx.fetch()` API
  - If SHA256 provided, verification happens at fetch time; mismatch causes hard failure
  - Never re-verified from cache
  - **Permissive by default**: SHA256 optional for all recipes
  - **Namespace rule**: `local.*` recipes never require SHA256 (files are local/trusted)
  - **Future strict mode**: Will require SHA256 for all non-`local.*` recipes

### Dependency Semantics (strong, weak, reference-only)

- **Strong dependencies** provide a complete spec (manifest or explicit `source`). They are instantiated immediately and run toward their target phase.
- **Weak dependencies** specify a query (`recipe = "name"` or partial identity) plus a fallback spec in `weak = { ... }`. The engine tries to satisfy the query from existing/manifest/other strong nodes; if no match, it spawns the fallback. Ambiguities raise errors with all candidates listed.
- **Reference-only dependencies** provide only a query (no `source`/`weak`). They must be satisfied by some other recipe in the graph; otherwise resolution fails after convergence.
- **Nested fetch prerequisites** live in `source.dependencies` and follow the same rules. They must complete (typically to `completion`) before the parent’s `recipe_fetch` runs.
- Resolution is iterative: the engine waits for all active recipes to reach their target phases, runs weak-resolution passes (matching or spawning fallbacks), and repeats while progress is made. Progress accounts for newly spawned fallbacks even when unresolved counts stay flat.

### Verbs

Recipes define verbs describing how to acquire, validate, and install packages:

- **`check`** — Test whether package is already satisfied (optional). Returns boolean or exit code. If absent, uses cache marker (`envy-complete`). Enables wrapping system package managers (apt, brew) without cache involvement.
- **`fetch`** — Acquire source materials. Can be:
  - String: `fetch = "https://..."` (no verification)
  - Single file: `fetch = {url="...", sha256="..."}` (optional verification)
  - Multiple files: `fetch = {{url="..."}, {url="..."}}` (concurrent, optional verification per-file)
  - Custom function: `fetch = function(ctx) ctx.fetch(...) end` (imperative with `ctx.fetch()` API)
  - Function returning declarative: `fetch = function(ctx) return "https://..." end` (enables templating with `ctx.options`; return value can be any declarative form: string, table, array; can mix with imperative `ctx.fetch()` calls)
- **`stage`** — Prepare staging area from fetched content. Default extracts archives; custom functions can manipulate source tree.
- **`build`** — Compile or process staged content. Recipes access staging directory, dependency artifacts, and install directory.
- **`install`** — Write final artifacts to install directory. On success, envy atomically renames to asset directory and marks complete.

### User-Managed vs Cache-Managed Packages

Envy supports two package models distinguished by CHECK verb presence:

**Cache-Managed Packages** (no CHECK verb):
- Artifacts stored in cache—hash-based lookup via `cache::ensure_asset()`
- Install writes to `lock->install_dir()`, calls `ctx.mark_install_complete()`
- Lock destructor renames `install/` → `asset/`, touches `envy-complete`
- Subsequent runs: cache hit (asset exists) skips all phases
- Full ctx API: `fetch_dir`, `stage_dir`, `install_dir`, `mark_install_complete()`, `extract_all()`, etc.
- Example: toolchains, libraries, build tools

**User-Managed Packages** (CHECK verb present):
- Artifacts live outside cache (system state, environment, user directories)
- CHECK verb tests satisfaction (string command or function returning bool)
- Lock marked user-managed via `lock->mark_user_managed()`—destructor purges entire `entry_dir` (ephemeral workspace)
- Install phase modifies system; CANNOT call `mark_install_complete()` (not exposed in ctx)
- Restricted ctx API: only `tmp_dir`, `run()`, `asset()`, `product()`, `identity`, `options`
- Forbidden: `fetch_dir`, `stage_dir`, `install_dir`, `mark_install_complete()`, `extract_all()`, `copy()`, `move()`, etc.
- Attempting forbidden APIs throws runtime error
- Example: brew/apt wrappers, environment setup, credential files

**Double-Check Lock Pattern (user-managed only):**
Coordinates concurrent processes, prevents duplicate work:
1. **Pre-lock check:** Run CHECK verb; if true (satisfied), skip all phases
2. **Acquire lock:** Mark user-managed, block while other process works
3. **Post-lock re-check:** Run CHECK again; if true (race—other process completed), release lock, purge entry_dir
4. **Install:** If still false, execute phases; destructor purges entry_dir after completion

Race example: Process A checks (false), waits for lock. Process B holds lock, installs Python via brew. B releases. A acquires lock, re-checks (true—Python now installed), releases, skips phases.

**Implementation mechanics:**
- Detection: `recipe_has_check_verb(r, lua)` → user vs cache path in `run_check_phase()`
- User-managed lock: `phase_check.cpp:169` calls `lock->mark_user_managed()`
- Lock destructor (`cache.cpp:158`): `if (user_managed_) { purge_entry_dir(); }` vs `if (completed_) { rename_install_to_asset(); }`
- Ctx isolation: `phase_install.cpp:67` builds restricted table, forbidden APIs throw errors

**Example: System Package Wrapper**
```lua
-- python.interpreter@v3 (user-managed)
IDENTITY = "python.interpreter@v3"

-- Check if Python already installed
CHECK = function(ctx)
  local result = ctx.run("python3 --version", {quiet = true})
  return result.exit_code == 0
end

-- Install via platform package manager
INSTALL = function(ctx)
  if ENVY_PLATFORM == "darwin" then
    ctx.run("brew install python3")
  elseif ENVY_PLATFORM == "linux" then
    ctx.run("sudo apt-get install -y python3")
  end
  -- No mark_install_complete() call—not exposed for user-managed packages
end
```

**Example: Cache-Managed Toolchain**
```lua
-- arm.gcc@v2 (cache-managed)
IDENTITY = "arm.gcc@v2"
-- No CHECK verb—hash-based cache lookup

FETCH = {url = "https://arm.com/gcc-13.2.0.tar.xz", sha256 = "abc..."}

STAGE = function(ctx) ctx.extract_all() end

INSTALL = function(ctx)
  ctx.copy(ctx.stage_dir .. "/gcc", ctx.install_dir)
  ctx.mark_install_complete()  -- Required—lock destructor renames install → asset
end
```

### Dependencies

Recipes declare dependencies; transitive resolution is automatic. Manifest authors specify only direct needs.

```lua
-- vendor.openocd@v3
DEPENDENCIES = {
  {
    recipe = "arm.gcc@v2",
    url = "https://github.com/arm/recipes/gcc-v2.lua",
    sha256 = "a1b2c3d4...",
    options = { version = "13.2.0" },
  },
}

BUILD = function(ctx)
  local gcc_root = ctx:asset("arm.gcc@v2")
  ctx:run("./configure", "--prefix=" .. ctx.install_dir, "CC=" .. gcc_root .. "/bin/arm-none-eabi-gcc")
  ctx:run("make", "-j" .. ctx.cores)
end

INSTALL = function(ctx)
  ctx:run("make", "install")
end
```

**Resolution:** Topological sort ensures dependencies deploy before dependents. Cycles error (must be DAG). Dependencies specify exact recipe versions—same recipe version always uses same deps (reproducible builds).

**Security:** Non-local recipes cannot depend on `local.*` recipes. Envy enforces at load time.

## Unified DAG Execution Model

### Overview

Envy builds a single `tbb::flow::graph` containing all recipe and package operations. No separation between "resolution" and "installation"—recipe fetching and asset building interleave as dependencies require. Graph expands dynamically: `recipe_fetch` nodes discover dependencies and add new nodes during execution.

### Phase Model

Each DAG node represents `(recipe_identity, options)` with up to seven verb phases:

- **`recipe_fetch`** — Load recipe Lua file(s) into cache; discover dependencies; add child nodes to graph
- **`check`** — Test if asset already satisfied (skip remaining phases if true)
- **`fetch`** — Download/acquire source materials into `fetch/`
- **`stage`** — Prepare build staging area from fetched content
- **`build`** — Compile or process staged content
- **`install`** — Write final artifacts to install directory
- **`deploy`** — Post-install actions (env setup, capability registration)

**Node optimization:** Only declared/inferred phases create nodes. Minimal recipes (just `source` field) infer `recipe_fetch` → `fetch` → `stage`, skip `build`/`install`/`deploy`. Recipe without `build` verb omits build node. Zero-verb overhead for simple cases.

**Phase execution:** Each phase is a `flow::continue_node`. Intra-node dependencies: `recipe_fetch` → `check` → `fetch` → `stage` → `build` → `install` → `deploy` (linear chain). Inter-node dependencies declared via `needed_by` annotation (see below).

### Recipe Fetching (Custom and Declarative)

**Identity requirement:** ALL recipes must declare their identity:
```lua
-- vendor.lib@v1 recipe file
IDENTITY = "vendor.lib@v1"

-- local.wrapper@v1 recipe file
IDENTITY = "local.wrapper@v1"

-- Rest of recipe...
```
Envy validates declared identity matches requested identity. This prevents typos, stale references, copy-paste errors, and malicious substitution. No namespace exemptions—all recipes require identity declaration.

**Declarative sources** (common case):
```lua
-- String shorthand (no verification)
FETCH = "https://example.com/gcc.tar.gz"

-- Single file with verification
FETCH = {url = "https://example.com/lib.lua", sha256 = "abc..."}

-- Multiple files (concurrent download)
FETCH = {
  {url = "https://example.com/gcc.tar.gz", sha256 = "abc..."},
  {url = "https://example.com/gcc.tar.gz.sig", sha256 = "def..."}
}

-- Git repository
FETCH = {url = "git://github.com/vendor/lib.git", ref = "a1b2c3d4..."}

-- S3 (first-class support)
FETCH = {url = "s3://bucket/lib.lua", sha256 = "ghi..."}
```

**Custom fetch functions** (exotic cases—JFrog, authenticated APIs, custom tools):
```lua
{
  recipe = "corporate.toolchain@v1",
  FETCH = function(ctx)
    local jfrog = ctx:asset("jfrog.cli@v2")  -- Access installed dependency

    -- Download files concurrently with verification
    local files = ctx.fetch({
      {url = "https://internal.com/recipe.lua", sha256 = "abc..."},
      {url = "https://internal.com/helpers.lua", sha256 = "def..."}
    })
    -- files = {"recipe.lua", "helpers.lua"}
  end,
  DEPENDENCIES = {
    { recipe = "jfrog.cli@v2", source = "...", sha256 = "...", needed_by = "recipe_fetch" }
  }
}
```

**Recipe fetch context API:**
```lua
ctx = {
  -- Identity & configuration
  IDENTITY = string,                                -- Recipe identity (recipes only, not manifests)
  options = table,                                  -- Recipe options (always present, may be empty)

  -- Directories
  tmp_dir = string,                                 -- Ephemeral temp directory for ctx.fetch() downloads

  -- Download functions (concurrent, atomic commit)
  FETCH = function(spec) -> string | table,         -- Download file(s), verify SHA256 if provided
                                                    -- spec: {url="...", sha256="..."} or {{...}, {...}}
                                                    -- Returns: basename(s) of downloaded file(s)

  -- Dependency access
  asset = function(identity) -> string,             -- Path to installed dependency

  -- Process execution
  run = function(cmd, ...) -> number,               -- Execute subprocess, stream output
  run_capture = function(cmd, ...) -> table,        -- Capture stdout/stderr/exitcode
}

-- Platform globals
ENVY_PLATFORM, ENVY_ARCH, ENVY_PLATFORM_ARCH
```

**Fetch behavior:**
- **Polymorphic API**: Single file `ctx.fetch({url="..."})` or batch `ctx.fetch({{url="..."}, {url="..."}})`
- **Concurrent**: All downloads happen in parallel via TBB task_group
- **Atomic**: All files downloaded and verified before ANY committed to fetch_dir (all-or-nothing)
- **SHA256 optional**: If provided, verified after download; if absent, permissive

**Verification:** SHA256 is **optional**. If `sha256` field present, Envy verifies after download. If absent, download proceeds without verification (permissive mode). Custom fetch functions cannot bypass—all downloads go through `ctx.fetch()` API. Future "strict mode" will require SHA256 for all non-`local.*` recipes.

**Cache layout:** Custom fetch → multi-file cache directory with `recipe.lua` entry point:
```
~/.cache/envy/recipes/
└── corporate.toolchain@v1/
    ├── envy-complete
    ├── recipe.lua           # Entry point (required)
    ├── helpers.lua
    ├── fetch/               # Downloaded files moved here after verification
    │   └── envy-complete
    └── work/
        └── tmp/             # Temp directory for ctx.fetch() (cleaned after)
```

### Phase Dependencies via `needed_by`

**Default behavior:** Recipe A depends on recipe B → A's `fetch` phase waits for B's last declared phase (usually `deploy`).

**Custom phase dependencies:** Use `needed_by` annotation to couple specific phases:
```lua
DEPENDENCIES = {
  { recipe = "jfrog.cli@v2", url = "...", sha256 = "...", needed_by = "recipe_fetch" }
}
```

**Semantics:** Dependency must complete its last declared phase before this node's specified phase starts. If `needed_by = "recipe_fetch"`, jfrog.cli's `deploy` completes before this recipe's `recipe_fetch` begins (recipe cannot be fetched until tool is installed).

**Concrete example (corporate JFrog workflow):**
```lua
-- Manifest packages
{
  {
    recipe = "corporate.toolchain@v1",
    FETCH = function(ctx)
      local jfrog = ctx:asset("jfrog.cli@v2")  -- Tool must be installed first
      -- ... fetch using jfrog CLI ...
    end,
    DEPENDENCIES = {
      {
        recipe = "jfrog.cli@v2",
        source = "https://public.com/jfrog-cli-recipe.lua",
        sha256 = "...",
        needed_by = "recipe_fetch"  -- Block corporate.toolchain recipe_fetch until jfrog.cli deployed
      }
    }
  }
}
```

**Graph topology:**
```
[jfrog.cli recipe_fetch] → [jfrog.cli fetch] → [jfrog.cli install] → [jfrog.cli deploy]
                                                                           ↓
                                              [corporate.toolchain recipe_fetch] → ...
```

**Valid `needed_by` phases:** `recipe_fetch`, `check`, `fetch`, `stage`, `build`, `install`, `deploy`. Omitting `needed_by` defaults to blocking on `fetch` (standard transitive dependency).

### Dynamic Graph Expansion

**Memoization:** Nodes keyed by `"identity{key1=val1,key2=val2}"` (canonical string, options sorted lexicographically). First thread to request a node allocates it; later threads reuse existing node.

**Expansion process:**
1. Manifest roots seed graph with initial `recipe_fetch` nodes
2. `recipe_fetch` node executes: fetch recipe file(s), load Lua, evaluate `dependencies` field
3. For each dependency: ensure memoized node exists, add edges based on `needed_by`
4. Child `recipe_fetch` nodes execute, discover their dependencies, add more nodes
5. Graph grows until all transitive dependencies discovered
6. `flow::graph::wait_for_all()` blocks until entire graph completes

**Cycle detection:** Must catch cycles during graph construction. Example illegal cycle:
```lua
-- Recipe A
{ recipe = "A@v1", fetch = function(ctx) ctx:asset("B@v1") end,
  DEPENDENCIES = { { recipe = "B@v1", needed_by = "recipe_fetch" } } }

-- Recipe B
{ recipe = "B@v1", dependencies = { { recipe = "A@v1", needed_by = "recipe_fetch" } } }
```
Both recipes need each other for `recipe_fetch` → deadlock. Envy detects via reachability check before adding edges; errors with cycle path.

### Command Execution Model

Commands implement `bool execute()` with no oneTBB types in their interface. Main wraps execution inside `tbb::task_arena().execute()` establishing a shared thread pool for all parallel operations.

**Simple commands:** Ignore parallelism—just do synchronous work and return success/failure.

**Package commands:** Build unified `flow::graph`, seed with manifest roots, wait for completion:
```cpp
bool cmd_install::execute() {
  unified_dag dag{ cache_, manifest_->packages() };
  dag.execute();  // Parallel internally (recipe_fetch + asset phases)
  print_summary(dag.roots());
  return true;
}
```

**Custom parallel commands:** Create local `flow::graph` or `task_group` when needed—all TBB operations share the arena's thread pool via work-stealing scheduler.

**Nested execution:** Commands freely call blocking parallel helpers from any context—direct invocation, inside `task_group` tasks, or within `flow::graph` node lambdas. TBB's cooperative scheduler handles blocking without deadlock; when a task waits on inner parallel work, other threads steal pending tasks. This composition works naturally because all TBB primitives share the single task arena established by main.

**Lifetime:** Stack-scoped TBB graphs in `execute()` naturally satisfy lifetime requirements—nodes complete before function returns, so no dangling references. Commands destroyed after `execute()` completes.

## Filesystem Cache

Cache layout, locking, verification, and recovery live in `docs/cache.md`.

## Platform-Specific Recipes

Recipes run only on host platform—no cross-deployment. Single recipe file adapts via platform variables envy provides. Authors structure platform logic however they want.

**Envy-provided globals:**
- `ENVY_PLATFORM` — `"darwin"`, `"linux"`, `"windows"`
- `ENVY_ARCH` — `"arm64"`, `"x86_64"`, `"aarch64"`, etc.
- `ENVY_PLATFORM_ARCH` — Combined: `"darwin-arm64"`, `"linux-x86_64"`
- `ENVY_OS_VERSION` — `"14.0"` (macOS), `"22.04"` (Ubuntu)

**Single-file with conditionals:**
```lua
FETCH = function(ctx)
  local version = ctx.options.version or "13.2.0"
  local hashes = {
    ["13.2.0"] = {
      ["darwin-arm64"] = "a1b2...", ["linux-x86_64"] = "c3d4...",
    },
  }

  return {
    url = string.format("https://arm.com/gcc-%s-%s-%s.tar.xz",
                       version, ENVY_PLATFORM, ENVY_ARCH),
    sha256 = hashes[version][ENVY_PLATFORM_ARCH],
  }
end

STAGE = function(ctx)
  ctx:extract_all()
end

INSTALL = function(ctx)
  ctx:add_to_path("bin")
  if ENVY_PLATFORM == "darwin" then
    ctx:fixup_macos_rpaths()
  end
end
```

**Multi-file with platform modules:**
```
arm.gcc@v2/
├── recipe.lua
├── darwin.lua
├── linux.lua
└── checksums.lua
```

```lua
-- recipe.lua
local impl = require(ENVY_PLATFORM)  -- Loads darwin.lua or linux.lua
FETCH = function(ctx) return impl.fetch(ctx, require("checksums")) end
STAGE = impl.stage
INSTALL = impl.install
```

**Platform validation:**
```lua
local SUPPORTED = { darwin = { arm64 = true }, linux = { x86_64 = true } }
assert(SUPPORTED[ENVY_PLATFORM] and SUPPORTED[ENVY_PLATFORM][ENVY_ARCH],
       "Unsupported platform: " .. ENVY_PLATFORM_ARCH)
```

## TUI / Output

### Stream Semantics

**Stdout:** Machine-readable output only—`envy hash`, `envy lua --eval`, future asset path queries. Never logs, progress, or diagnostics.

**Stderr:** All human communication—logs, progress bars, warnings, errors. Thread-safe queue-based rendering.

### Immediate-Mode Architecture

**Stateless rendering:** TUI is pure function `(frame, width, now, ansi) → string`. Workers cache section frames; renderer reads at 30fps. No animation state—spinners computed from timestamps, progress bars show current values. Benefits: deterministic output, full unit testability, no state sync.

**Section state:** Vector of sections (allocation order = render order). Each: handle, active flag, cached frame. Worker thread calls `section_set_content(handle, frame)` on progress events; main thread renders all active sections each cycle.

**Render cycle (30fps):**
1. ANSI: clear previous progress region (cursor up, clear to end). Fallback: no-op.
2. Flush log queue (logs print in cleared space)
3. Get terminal width (syscall), current time
4. For each active section: `render_section_frame(cached_frame, width, ansi, now)`
5. ANSI: update line count for next clear. Fallback: throttle (2s), print if changed.

**Critical ordering:** Clear BEFORE flush prevents logs from being erased. Logs print where old progress was; new progress renders below logs.

**Section frame types:**
- `progress_data`: percent (0-100), status string → `[label] status [=====>   ] 42.5%`
- `text_stream_data`: line buffer, line_limit, start_time → last N lines of build output with spinner
- `spinner_data`: text, start_time, frame_duration → animated `|/-\` computed from elapsed time
- `static_text_data`: text → `[label] text`

**Interactive mode:** Global mutex serializes recipes needing terminal control (sudo, installers). Acquire locks, pauses rendering; release unlocks, resumes. RAII guard available.

**Integration:** Phases delegate TUI management to `tui_actions` helpers (`run_progress`, `fetch_progress_tracker`, `extract_progress_tracker`)—single-responsibility, consistent formatting, testable in isolation. `ctx.run()` auto-creates `run_progress` when recipe has `tui_section`; Lua code gets TUI integration automatically.

**Test API:** `#ifdef ENVY_UNIT_TEST` exposes `g_terminal_width`, `g_isatty`, `g_now` globals and `test::render_section_frame()` for pure rendering tests without TUI thread.

### Log Formatting

**Plain mode** (`init(std::nullopt)`): Clean output, no timestamps/severity prefixes. Threshold = info.
```
Fetching gcc-13.2.0.tar.xz...
Warning: Recipe deprecated
Error: SHA256 mismatch
```

**Structured mode** (explicit `-v/--verbose` flag): Timestamps + severity on all messages.
```
[2024-10-19 12:34:56.123] [DEBUG] Cache miss for arm.gcc@v2
[2024-10-19 12:34:56.234] [INFO] Fetching gcc-13.2.0.tar.xz...
[2024-10-19 12:34:56.789] [WARN] Recipe deprecated
```

### Thread Model

Main thread runs TUI render loop; TBB workers push to thread-safe queues. Uniform 16ms log refresh; progress refresh at 16ms (TTY) or 1024ms (non-TTY). Workers call `tui::is_tty()` to choose progress bar style (animated vs. periodic snapshots). Progress library handles ANSI clear/redraw; TUI owns timing and queue orchestration.

### API Surface

```cpp
namespace envy::tui {
  enum class level { debug, info, warn, error };

  // Lifecycle
  void init(std::optional<level> threshold);  // nullopt = plain mode
  void run();                                 // Blocking render loop
  void shutdown();                            // Signal exit, flush queues
  bool is_tty();                              // Expose isatty(STDERR_FILENO)

  // Logging to stderr (thread-safe, printf-style, queued)
  void debug(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
  void info(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
  void warn(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
  void error(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

  // Stdout (direct write, bypasses TUI, never queued)
  void print_stdout(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

  // Progress (thread-safe, retained-mode handles)
  struct progress_config { std::string label; /* style, type, etc. */ };
  int create_progress(progress_config cfg);
  void update_progress(int handle, float percent);
  void complete_progress(int handle);

  // Rendering control (for interactive subprocess handoff)
  void pause_rendering();   // Stop render loop, clear progress bars
  void resume_rendering();  // Restart render loop

  // Output redirection (for testing)
  void set_output_handler(std::function<void(std::string_view)> fn);
}
```

**Implementation:** Flat namespace with module-internal state (global mutexes, queues, atomics)—avoids singleton boilerplate while maintaining single logical instance. Single log queue protected by mutex. Workers format messages via `vsnprintf`, append to queue. Progress state stored in retained-mode map—workers update percentage via handle whenever desired. Main thread drains log queue at 16ms intervals, always flushes logs, renders current progress state at 16ms (TTY) or 1024ms (non-TTY). Atomic bools for shutdown and pause coordination. Non-TTY mode skips ANSI codes.

### Interactive Input & Process Spawning

**REPL mode** (`envy lua`): TUI runs in interactive mode—logs bypass queue, go straight to stderr via `fprintf`. No render loop, no progress bars.

**Recipe process execution:** Three modes via `ctx` API:

- **`run_capture(cmd, args)`** — Stdout/stderr piped to string, returned to Lua. Stdin closed. No TUI interaction (silent checks like `brew list | grep foo`).
- **`run(cmd, args)`** — Stdout/stderr piped line-by-line to `tui::info()`. Stdin closed. No TUI pause (build output appears as logs).
- **`run_interactive(cmd, args)`** — Stdin/stdout/stderr inherited. TUI calls `pause_rendering()`, clears progress bars, waits for subprocess exit, calls `resume_rendering()`. Render loop idles on atomic flag.

**Platform abstraction:** Unix uses `fork()`/`execvp()`/`pipe()`/`dup2()`. Windows uses `CreateProcess()`/`STARTUPINFO` with redirected handles. Both hide behind `envy::process` interface. Terminal control via `isatty()`/`_isatty()` + ANSI escape codes (Windows 10+ `ENABLE_VIRTUAL_TERMINAL_PROCESSING` via `SetConsoleMode`).

## Testing

### Unit Tests

Side-by-side with source: `src/cache/lock.cpp` + `src/cache/lock_test.cpp`. Doctest C++ single-file amalgamation; automatic registration. All test `.cpp` files compiled directly (no static archive) into `out/build/envy_unit_tests` executable. Runs as CMake build step; touches `out/build/envy_unit_tests.timestamp` on exit 0. Top-level targets: `envy` tool + test timestamp. `./build.sh` builds everything—tests run automatically.

### Functional Tests

Python 3.13+ stdlib only (`unittest`—no third-party deps). Located in `functional_tests/` flat (no subdirs). Parallel execution; each test uses isolated cache directory via `ENVY_TEST_ID` environment variable. Per-test cleanup via fixtures (context managers).

**Cache testing:** Uses `envy_functional_tester` binary—production `envy` with additional testing commands conditionally compiled via `ENVY_FUNCTIONAL_TESTER=1` define. Same `main.cpp`, same CLI11 parsing, same TBB async execution. Tests invoke cache C++ API directly via CLI without requiring Lua recipes/manifests. Barrier synchronization (`--barrier-signal`, `--barrier-wait`) enables deterministic concurrency testing via filesystem coordination. Key-value output format (`locked=true\npath=/foo/bar\n...`) provides observability without JSON library dependency. Commands: `envy_functional_tester cache ensure-asset <identity> <platform> <arch> <hash>`, `envy_functional_tester cache ensure-recipe <identity>`. Flags: `--cache-root`, `--test-id`, `--barrier-signal`, `--barrier-wait`, `--crash-after`, `--fail-before-complete`.

**Recipe testing (future):** Test recipes embedded as string literals, written to temp dirs—namespace `functionaltest.*` (e.g., `functionaltest.gcc@v1`). Recipes use filesystem `fetch` for speed; HTTP tests spawn local servers separately.

**CI:** GitHub Actions on Darwin/Linux/Windows × x64/arm64.

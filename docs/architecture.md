# Envy Architecture

## Project Manifests

**Syntax:** Shorthand `"namespace.name@version"` expands to `{ recipe = "namespace.name@version" }`. Table syntax supports `url`, `sha256`, `file`, `options`.

**Platform-specific packages:** Manifests are Lua scripts—use conditionals and `envy.join()` to combine common and OS-specific package lists.

```lua
-- project/envy.lua
local common = {
    {  -- Remote with verification and options
        recipe = "arm.gcc@v2",
        url = "https://github.com/arm/recipes/gcc-v2.lua",
        sha256 = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        options = { version = "13.2.0", target = "arm-none-eabi" },
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

packages = ENVY_PLATFORM == "darwin" and envy.join(common, darwin_packages)
        or ENVY_PLATFORM == "linux" and envy.join(common, linux_packages)
        or common
```

**Uniqueness validation:** Envy validates manifests post-execution. Duplicate recipe+options combinations error (deep comparison—string `"foo@v1"` matches `{ recipe = "foo@v1" }`). Same recipe with conflicting sources (different `url`/`sha256`/`file`) errors. Same recipe+options from identical sources is duplicate. Different options yield different deployments—allowed.

### Overrides

Manifests can override recipe sources globally. Useful for mirrors, internal caches, or local development.

```lua
-- project/envy.lua
overrides = {
  ["arm.gcc@v2"] = {
    url = "https://internal-mirror.company/recipes/arm-gcc-v2.lua",
    sha256 = "a1b2c3d4e5f6..."
  },
  ["gnu.binutils@v3"] = {
    file = "./local-recipes/binutils.lua"  -- Local development
  }
}

packages = {
  "arm.gcc@v2",  -- Will use overridden source
  {
    recipe = "vendor.openocd@v3",
    url = "https://example.com/openocd.lua",
    sha256 = "..."
    -- openocd's dependency on arm.gcc@v2 will also use overridden source
  }
}
```

**Semantics:** Overrides apply to all recipe references (manifest packages + transitive dependencies). Override source replaces original source; options unchanged. Conflicting overrides for same recipe error at manifest validation.

## Recipes

### Organization

**Identity:** Recipes are namespaced with version: `arm.gcc@v2`, `gnu.binutils@v3`. The `@` symbol denotes **recipe version**, not asset version. Asset versions come from `options` in manifest. Multiple recipe versions coexist; `local.*` namespace reserved for project-local recipes.

**Sources:**
- **Built-in:** Embedded in binary (`envy.*` namespace), extracted to cache on first run
- **Remote:** Fetched from `url`, verified via `sha256`, cached
- **Project-local:** Loaded from `file` path, never cached

**Formats:**
- **Single-file:** `.lua` file (default, preferred)
- **Multi-file:** Archive (`.tar.gz`/`.tar.xz`/`.zip`) with `recipe.lua` entry point at root

**Integrity:** Remote recipes require SHA256 hash in manifest. Envy verifies before caching/executing. Mismatch causes hard failure.

### Dependencies

Recipes declare dependencies; transitive resolution is automatic. Manifest authors specify only direct needs.

```lua
-- vendor.openocd@v3
identity = "vendor.openocd@v3"

function make_depends(options)
    return {
        {
            recipe = "arm.gcc@v2",
            url = "https://github.com/arm/recipes/gcc-v2.lua",
            sha256 = "a1b2c3d4...",
            options = { version = options.armgcc_version or "13.2.0" },
        },
    }
end

deploy = function(ctx)
    local gcc_root = asset("arm.gcc@v2")  -- Access cached asset
    ctx.extract_all()
    ctx.run("./configure", "--prefix=" .. ctx.install_dir, "CC=" .. gcc_root .. "/bin/arm-none-eabi-gcc")
    ctx.run("make", "-j" .. ctx.cores)
    ctx.run("make", "install")
end
```

**Resolution:** Topological sort ensures dependencies deploy before dependents. Cycles error (must be DAG). Dependencies specify exact recipe versions—same recipe version always uses same deps (reproducible builds).

**Security:** Non-local recipes cannot depend on `local.*` recipes. Envy enforces at load time.

## Manifest Loading & Recipe Resolution

### Manifest Execution

**Discovery:** Search upward from CWD for `envy.lua`, stop at filesystem root or git boundary.

**Execution:** Run manifest in fresh `lua_state` with envy globals (`ENVY_PLATFORM`, `ENVY_ARCH`, `envy.join()`, etc.). Extract `packages` table and optional `overrides` table, normalize shorthand strings into full tables, validate duplicates and conflicting sources.

### Resolution Summary

**Unified resolver:** Single recursive step fetches the recipe, evaluates its `dependencies` table/function, memoizes the node by `(identity, options)`, parallelizes child work via oneTBB, and enforces cycles, security policy, and `needed_by` checks while materializing the DAG. See `docs/recipe_resolution.md` for the detailed contract.

## Command Execution Model

Commands implement `bool execute()` with no oneTBB types in their interface. Main wraps execution inside `tbb::task_arena().execute()` establishing a shared thread pool for all parallel operations.

**Simple commands:** Ignore parallelism—just do synchronous work and return success/failure.

**Recipe commands:** Call blocking helpers (`resolve_recipes()`, `ensure_assets()`) that encapsulate parallel implementation via `task_group` (resolution) and `flow::graph` (verb DAG execution). After parallel phases complete, commands perform sequential post-processing (validation, summary printing, shell script generation).

**Custom parallel commands:** Create local `flow::graph` or `task_group` when needed—all TBB operations share the arena's thread pool via work-stealing scheduler.

**Nested execution:** Commands freely call blocking parallel helpers from any context—direct invocation, inside `task_group` tasks, or within `flow::graph` node lambdas. TBB's cooperative scheduler handles blocking without deadlock; when a task waits on inner parallel work, other threads steal pending tasks. This composition works naturally because all TBB primitives (`task_group`, `flow::graph`, `parallel_invoke`) share the single task arena established by main.

**Lifetime:** Stack-scoped TBB graphs in `execute()` naturally satisfy lifetime requirements—nodes complete before function returns, so no dangling references. Commands destroyed after `execute()` completes.

**Example patterns:**
```cpp
// Simple command
bool cmd_version::execute() {
  tui::info("envy version %s", ENVY_VERSION_STR);
  return true;
}

// Recipe command with blocking helpers
bool cmd_install::execute() {
  auto resolved = resolve_recipes(manifest_->packages());  // Parallel internally
  ensure_assets(resolved);                                  // Parallel internally
  print_summary(resolved);                                  // Sequential
  return true;
}

// Custom parallel command
bool cmd_complex::execute() {
  tbb::flow::graph g;
  // Build custom DAG topology...
  g.wait_for_all();
  return finalize_results();
}
```

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
identity = "arm.gcc@v2"

fetch = function(options)
    local version = options.version or "13.2.0"
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

deploy = function(ctx)
    ctx.extract_all()
    ctx.add_to_path("bin")
    if ENVY_PLATFORM == "darwin" then
        ctx.fixup_macos_rpaths()
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
identity = "arm.gcc@v2"
local impl = require(ENVY_PLATFORM)  -- Loads darwin.lua or linux.lua
fetch = function(opts) return impl.fetch(opts, require("checksums")) end
deploy = impl.deploy
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

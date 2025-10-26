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
    local gcc_root = asset("arm.gcc@v2")  -- Access deployed dependency
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

**Execution:** Run manifest in fresh `lua_state` with envy globals (`ENVY_PLATFORM`, `ENVY_ARCH`, `envy.join()`, etc.). Extract `packages` table and optional `overrides` table.

**Normalization:** Expand shorthand strings (`"arm.gcc@v2"`) to full table form (`{ recipe = "arm.gcc@v2" }`).

**Validation:** Check for duplicates (same recipe+options), conflicting sources (same identity+options, different url/sha256), malformed entries.

### Resolution: Two-Phase Algorithm

**Phase 1: Parallel Recipe Acquisition**

Fetch all recipes (including transitive dependencies) into cache using TBB task parallelism:

```cpp
tbb::concurrent_hash_map<recipe_key, fs::path> recipe_cache;
tbb::concurrent_set<resolved_key> seen;

fetch_recipes(pkg, overrides, task_group):
  key = (pkg.identity, pkg.options)
  if !seen.insert(key): return

  task_group.run([=] {
    src = apply_overrides(pkg, overrides)
    recipe_key = (pkg.identity, src.sha256)

    path = cache::ensure_recipe(pkg.identity, src.sha256, src.url)
    recipe_cache.insert(recipe_key, path)

    recipe = execute_recipe(new_lua_state(), path)
    if recipe.make_depends:
      for dep in recipe.make_depends(pkg.options):
        fetch_recipes(dep, overrides + recipe.overrides, task_group)
  })
```

Each recipe spawns TBB task. `cache::ensure_recipe` handles lock contention—first task fetches, others wait and hit cache. Different recipes fetch concurrently. Local recipes (`local.*`) never cached, loaded directly from project tree.

**Phase 2: Serial Dependency Resolution**

Build resolved recipe graph via imperative DFS. Fast (milliseconds) since all recipes cached:

```cpp
map<resolved_key, resolved_recipe> visited;
set<resolved_key> visiting;  // Cycle detection
vector<error> errors;

resolved_recipe* resolve(pkg, overrides):
  key = (pkg.identity, pkg.options)
  if visited.contains(key): return &visited[key]

  if visiting.contains(key):
    errors.push_back("Cycle detected")
    return nullptr
  visiting.insert(key)

  src = apply_overrides(pkg, overrides)
  path = recipe_cache[(pkg.identity, src.sha256)]

  recipe = execute_recipe(new_lua_state(), path)
  verbs = introspect_verbs(recipe)  // Check which verbs implemented

  deps = []
  if recipe.make_depends:
    for dep in recipe.make_depends(pkg.options):
      if dep.needed_by && !verbs.contains(dep.needed_by):
        errors.push_back("needed_by='{dep.needed_by}' but no such verb")

      deps.push_back(resolve(dep, overrides + recipe.overrides))

  visiting.erase(key)
  visited[key] = {identity, options, verbs, deps, recipe}
  return &visited[key]
```

**Node uniqueness:** Graph node = `(recipe_identity, options)`. Same recipe with different options yields different nodes, different cache keys, different deployments. Same recipe with identical options merges to single node regardless of which dependents reference it.

**Override semantics:** Manifest overrides trump all. Recipe can override its dependencies' sources and options via `overrides` table in `make_depends()`. Overrides flow down: child sees parent's overrides plus parent's new declarations.

**Cycle detection:** Track `visiting` set during DFS. Back-edge (revisiting node in current path) indicates cycle. Accumulate errors, report batch at end.

**Validation:** Verify `needed_by` references implemented verb. Collect errors during resolution, fail after full traversal to report all issues at once.

### Verb Dependencies

Recipes declare which verb needs each dependency completed:

```lua
make_depends = function(options)
  return {
    {
      recipe = "envy.artifactory@v1",
      options = { version = "2.50.0" },
      needed_by = "fetch"  -- jfrog CLI needed before I can fetch
    },
    {
      recipe = "arm.gcc@v2",
      options = { version = "13.2.0" },
      needed_by = "build"  -- gcc needed before I can build
    },
    {
      recipe = "envy.python@v3",
      options = { version = "3.13" }
      -- No needed_by: must complete before I start ANY verb
    }
  }
end
```

**Semantics:** `needed_by = "build"` means dependency fully completes (all its verbs: fetch → build → install → deploy) before this recipe's `build` starts. No `needed_by` field = dependency completes before this recipe's first verb (most conservative). Error if `needed_by` references unimplemented verb.

**Rationale:** Tools like Artifactory's `jf` CLI needed for fetch; compilers like gcc needed for build. Explicit control enables fetch parallelism while respecting real constraints.

## Filesystem Cache

Envy maintains a user-wide cache at `~/.cache/envy/` (customizable) containing recipes (Lua scripts) and deployed assets (toolchains, SDKs). Cache entries are immutable once marked complete—enabling lock-free reads while using shared/exclusive locks only during creation.

### Directory Structure

```
~/.cache/envy/
├── recipes/
│   ├── envy.cmake@v1.lua                   # Built-in single-file
│   ├── arm.gcc@v2.lua                      # Remote single-file
│   └── gnu.binutils@v3/                    # Multi-file (extracted archive)
│       ├── recipe.lua
│       └── helpers.lua
├── deployed/
│   └── {namespace}.{name}@{version}/
│       └── {platform}-{arch}-sha256-{hash}/
│           ├── .envy-complete
│           ├── .envy-fingerprint.blake3
│           └── [unpacked asset tree]
└── locks/
    └── {recipe|deployed}.*.lock
```

**Project-local recipes** (`local.*` namespace) live in project tree, never cached:
```
project/envy/recipes/
├── local.tool@v1.lua
└── local.complex@v2/
    ├── recipe.lua
    └── helpers.lua
```

### Cache Keys

**Recipe:** `{namespace}.{name}@{version}.lua` or `{namespace}.{name}@{version}/`

**Deployment:** `{namespace}.{name}@{recipe_version}.{platform}-{arch}-sha256-{hash}` where `hash` is first 16 hex chars of archive SHA256. Key is deterministic before download, enabling early lock acquisition.

### Immutability & Locking

Once `.envy-complete` exists, entry is immutable. Future reads are lock-free.

**Implementation:**
- Linux/macOS: POSIX `fcntl(F_SETLKW)` with `F_WRLCK` (exclusive, blocking)
- Windows: `LockFileEx` with `LOCKFILE_EXCLUSIVE_LOCK` (exclusive, blocking)

**Patterns:**
1. **Lock-free read:** Check `.envy-complete`, use immediately if present
2. **Exclusive creation:** Acquire exclusive lock (blocks until available), check `.envy-complete` again (another process may have finished while waiting), create if still missing

**Lock file lifecycle:** Lock files (`locks/{recipe|deployed}.*.lock`) created on first lock acquisition, deleted when work completes—`cache::scoped_entry_lock` destructor unlinks after committing staging. Caller must invoke `mark_complete()` on success; destructor commits and writes `.envy-complete` only if marked. Exception before `mark_complete()` leaves entry incomplete—no marker written, staging abandoned. Lock-free reads never need lock files; they exist only during active deployment.

**Staging (automatic):** `.inprogress/` directory created automatically when lock acquired—clients never call `create_staging()`. Path returned in `ensure_result` is staging directory when locked. Located in cache directory (not OS temp) for three reasons: (1) atomic rename requires same filesystem—OS temp often different mount, forcing slow recursive copy; (2) disk locality—large toolchains (1-10GB) extract on cache filesystem avoiding temp partition exhaustion; (3) predictable location for cleanup. Staging path: `{entry_path}.inprogress/`. Staging mandatory for all entries (single-file and multi-file)—uniform API, zero rename overhead compared to fetch/extract costs.

**Commit (automatic):** Destructor commits staging via `platform::atomic_rename()` if `mark_complete()` called. Marker written to staging before commit—appears atomically with directory. Clients never call `commit_staging()`; entirely internal.

**Crash recovery:** Stale `.inprogress/` directories cleaned up lazily per-entry when lock is acquired. Before creating new staging, check if `{entry_path}.inprogress/` exists and remove it. No cache-wide scanning required—cleanup happens only for the specific entry being worked on.

### BLAKE3 Fingerprints

`.envy-fingerprint.blake3` stores BLAKE3 hash of every file in deployment. Binary format optimized for mmap: fixed header (magic/version/count/offsets), entry array (path offset/length, 32-byte hash, size, mtime), string table. User verification maps read-only, compares hashes without locks.

### Security Model

**Recipe verification:** Manifest provides SHA256; envy verifies before cache/execute. Missing hash warns unless `allow_unverified_recipes = true`.

**Asset verification:** Recipes declare SHA256 for downloads; envy verifies before extraction.

**Trust chain:** Verified recipe establishes trust anchor. Assets/logic declared by verified recipe are trusted transitively.

## Use Cases

**First deployment:** Check `.envy-complete` (missing) → acquire exclusive lock → staging auto-created, path returned → download → verify SHA256 → extract to path → compute BLAKE3 fingerprints → call `mark_complete()` → destructor writes `.envy-complete` to staging + atomic rename → release lock. Future reads lock-free.

**Concurrent deployment:** Process A acquires exclusive, begins work. Process B blocks on lock, waits. A completes, destructor commits. B unblocks, rechecks `.envy-complete` (now present), returns final path immediately. No duplicate download.

**Crash recovery:** Process A crashes mid-extraction. OS releases lock, destructor never runs. Process B acquires exclusive, removes stale `.inprogress/`, creates fresh staging, completes normally.

**Multi-project sharing:** Projects A and B both request `arm.gcc@v2` with `options.version="13.2.0"`. Same deployment key → cache hit → zero duplication.

**Recipe collision:** Projects A and B declare `arm.gcc@v2` with different SHA256 hashes. First to run caches recipe. Second project loads, computes hash, detects mismatch, errors: "Recipe integrity failed. Author should publish new version (e.g., arm.gcc@v3)."

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

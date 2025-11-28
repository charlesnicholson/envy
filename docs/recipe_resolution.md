# Unified DAG Resolution and Execution

## Core Model

**Single flow::graph:** Recipe fetching and asset building unified in one graph. No separation between "resolution" and "installation" phases—operations interleave as `needed_by` dependencies require.

**Node identity:** `(recipe_identity, options)` uniquely describes a graph node. Memoization key is canonical string `"identity{key1=val1,key2=val2}"` with options sorted lexicographically. Empty options omit braces. String keys enable trivial hashing, debuggability, and future serialization.

**Node phases:** Up to seven `flow::continue_node` instances per recipe:
- `recipe_fetch` — Load recipe Lua file(s) into cache; discover dependencies; add child nodes to graph
- `check` — Test if asset already satisfied
- `fetch` — Download/acquire source materials
- `stage` — Prepare staging area
- `build` — Compile/process
- `install` — Write final artifacts
- `deploy` — Post-install actions

**Phase dependencies:** Intra-node linear chain (`recipe_fetch` → `check` → `fetch` → ... → `deploy`). Inter-node via `needed_by` annotation in `dependencies` field. Only declared/inferred phases create nodes (node count optimization).

**Dependencies field:** Recipe may define `dependencies = { ... }` (static table) or `dependencies = function(ctx) ... end` (dynamic). Both forms normalize to plain Lua table before graph expansion continues. Each dependency entry may include `needed_by` field (default: `"fetch"`).

**Recipe object:** DAG node carrying `recipe::cfg` (identity, source, options), `lua_state_ptr` (for verb execution), and dependency pointers. Each recipe owns its Lua state; verbs query this state at execution time.

## Graph Construction

**Seed:** Manifest packages create initial `recipe_fetch` nodes in `flow::graph`. Broadcast kickoff node triggers parallel execution.

**Dynamic expansion:** `recipe_fetch` node body:
1. Fetch recipe file(s) (declarative `source` or custom `fetch` function)
2. Verify integrity (SHA256 for URLs if provided, git commit SHA, or API-enforced per-file verification)
3. Load Lua chunk, create `lua_state` sandbox
4. **Validate identity:** Non-`local.*` recipes must declare `identity = "..."` field matching referrer's identity (prevents wrong recipe substitution)
5. Evaluate `dependencies` field → plain table (static copy or function return value)
6. For each dependency: ensure memoized node exists (canonical key lookup), add edges based on `needed_by`
7. Complete `recipe_fetch` node, unblock dependent phases

**Memoization:** `concurrent_hash_map<string, dag_node*>` tracks nodes by canonical key. First thread to request a node allocates and initializes it; later threads reuse existing node. Prevents duplicate work when multiple recipes depend on same `(identity, options)`.

**Concurrency:** TBB `flow::graph` scheduler handles topological execution—nodes execute when predecessors complete. Work-stealing maximizes CPU utilization. Graph grows dynamically as `recipe_fetch` nodes add children, but topology remains acyclic (enforced by cycle detection).

## Phase Coupling via `needed_by`

**Default coupling:** Recipe A depends on recipe B → A's `fetch` waits for B's last declared phase (typically `deploy`).

**Custom coupling:** Annotate dependency with `needed_by` field:
```lua
dependencies = {
  { recipe = "jfrog.cli@v2", source = "...", sha256 = "...", needed_by = "recipe_fetch" }
}
```

**Semantics:** Dependency must complete its last declared phase before this node's specified phase starts. Example: `needed_by = "recipe_fetch"` → jfrog.cli's `deploy` phase completes before this recipe's `recipe_fetch` begins.

**Valid phases:** `recipe_fetch`, `check`, `fetch`, `stage`, `build`, `install`, `deploy`. Phase must exist in dependent node (e.g., can't use `needed_by = "build"` if dependent recipe has no build verb).

**Graph edges:** If `needed_by = "recipe_fetch"`, Envy creates edge `dependency.last_phase → this.recipe_fetch`. If `needed_by = "build"`, edge is `dependency.last_phase → this.build`.

**Concrete example (JFrog bootstrap):**
```lua
-- Manifest
{
  recipe = "corporate.toolchain@v1",
  fetch = function(ctx)
    local jfrog = ctx:asset("jfrog.cli@v2")  -- Requires jfrog CLI installed
    local tmp = ctx.tmp_dir
    ctx:run(jfrog .. "/bin/jfrog", "rt", "download", "recipes/toolchain.lua", tmp .. "/recipe.lua")
    ctx:commit_fetch({filename = "recipe.lua", sha256 = "abc123..."})
  end,
  dependencies = {
    {
      recipe = "jfrog.cli@v2",
      source = "https://public.com/jfrog-cli-recipe.lua",
      sha256 = "def456...",
      needed_by = "recipe_fetch"  -- Block corporate.toolchain recipe_fetch until jfrog.cli deployed
    }
  }
}
```

**Resulting graph:**
```
[jfrog.cli recipe_fetch] → [jfrog.cli fetch] → [jfrog.cli install] → [jfrog.cli deploy]
                                                                           ↓
                                              [corporate.toolchain recipe_fetch] → [corporate.toolchain fetch] → ...
```

jfrog.cli fully installs before corporate.toolchain recipe can be fetched. Recipe fetch function uses `ctx:asset("jfrog.cli@v2")` to access installed jfrog binary.

## Cycle Detection

**Requirement:** Must detect cycles during graph construction before execution begins. Cycles cause deadlock in `flow::graph`.

**Detection strategy:** Before adding edge `A.phase_x → B.phase_y`, verify B is not reachable from A via existing edges. If reachable, adding edge creates cycle—error with cycle path.

**Illegal cycle example:**
```lua
-- Recipe A
{ recipe = "A@v1", fetch = function(ctx) ctx:asset("B@v1") end,
  dependencies = { { recipe = "B@v1", needed_by = "recipe_fetch" } } }

-- Recipe B (inside A's dependency tree somewhere)
{ recipe = "B@v1", dependencies = { { recipe = "A@v1", needed_by = "recipe_fetch" } } }
```

Both need each other for `recipe_fetch` → deadlock. Envy detects when B's `recipe_fetch` node tries to add A as child: A is already ancestor of B in graph.

**Error message:** "Cycle detected: A@v1.recipe_fetch → B@v1.recipe_fetch → A@v1.recipe_fetch"

**Phase-agnostic cycles:** Standard recipe-to-recipe cycles also detected (A depends on B depends on A, regardless of `needed_by`). Thread-local stack tracks active `recipe_fetch` operations; encountering unfinished entry signals cycle.

## Recipe Fetching

### Identity Declaration

**Requirement:** ALL recipes must declare their identity at the top of the recipe file:
```lua
-- vendor.lib@v1 recipe file
identity = "vendor.lib@v1"

-- local.wrapper@v1 recipe file
identity = "local.wrapper@v1"

-- Rest of recipe...
```

**Validation:** When Envy loads a recipe, it verifies the declared identity matches the requested identity. This prevents:
- Accidental recipe substitution (wrong URL, typo in manifest)
- Copy-paste errors (forgot to update identity after copying recipe)
- Stale references (manifest not updated after recipe rename)
- Malicious substitution (compromised server for remote recipes)

**No exemptions:** Even `local.*` recipes require identity declaration. This is a correctness check orthogonal to SHA256 verification (which is about network trust). Identity validation catches errors in ALL recipes, regardless of namespace.

### Declarative Fetch Sources

**Single file** (string shorthand, no verification):
```lua
fetch = "https://example.com/gcc.tar.gz"
```

**Single file with verification** (table):
```lua
fetch = {url = "https://example.com/gcc.tar.gz", sha256 = "abc123..."}
```

**Multiple files** (concurrent download):
```lua
fetch = {
  {url = "https://example.com/gcc.tar.gz", sha256 = "abc123..."},
  {url = "https://example.com/gcc.tar.gz.sig", sha256 = "def456..."}
}
```

**Trust model:** SHA256 is **optional** (permissive by default). If provided, Envy verifies after download. Future "strict mode" will require SHA256 for all non-`local.*` recipes.

**Git sources:**
```lua
fetch = {url = "git://github.com/vendor/lib.git", ref = "a1b2c3d4..."}
```
`ref` can be commit SHA (self-verifying) or tag/branch (future strict mode requires SHA).

### Custom Fetch Functions

**Use case:** Authenticated sources, custom tools (JFrog, Artifactory), dynamic recipe generation, templating with `ctx.options`.

**Imperative mode** (calls `ctx.fetch()` + `ctx.commit_fetch()`):
```lua
{
  recipe = "corporate.toolchain@v1",
  fetch = function(ctx)
    local jfrog = ctx:asset("jfrog.cli@v2")  -- Access installed dependency

    -- Download files concurrently with verification
    ctx.fetch({
      {url = "https://internal.com/toolchain.tar.gz", sha256 = "abc123..."},
      {url = "https://internal.com/helpers.lua", sha256 = "def456..."}
    })
    ctx.commit_fetch({"toolchain.tar.gz", "helpers.lua"})
  end,
  dependencies = {
    { recipe = "jfrog.cli@v2", source = "...", sha256 = "...", needed_by = "recipe_fetch" }
  }
}
```

**Declarative return mode** (returns declarative spec):
```lua
{
  recipe = "vendor.gcc@v2",
  fetch = function(ctx)
    -- Template URL with options
    local version = ctx.options.version or "13.2.0"
    local arch = ctx.options.arch or ENVY_ARCH
    return {
      url = string.format("https://arm.com/gcc-%s-%s.tar.gz", version, arch),
      sha256 = ctx.options.sha256
    }
  end
}
```

**Mixed mode** (imperative + declarative):
```lua
fetch = function(ctx)
  -- Fetch authenticated file imperatively
  local secret = os.getenv("API_TOKEN")
  ctx.fetch("https://internal.com/file?token=" .. secret)
  ctx.commit_fetch("file")

  -- Return declarative spec for public files
  return {
    {url = "https://public.com/gcc.tar.gz", sha256 = "abc123..."},
    {url = "https://public.com/gcc.sig", sha256 = "def456..."}
  }
end
```

**Return value semantics:**
- `nil` or no return → imperative mode only
- String → declarative shorthand (no SHA256)
- Table → declarative spec (single file or array)
- All declarative forms supported: `"url"`, `{url="..."}`, `{{"url1"}, {"url2"}}`, `{"url1", "url2"}`

**Security boundary:** Custom fetch functions cannot access cache paths directly. All downloads go through `ctx.fetch()` API, which enforces optional SHA256 verification.

### Fetch Phase Context API

**Available to `function fetch(ctx)` in recipes:**
```lua
ctx = {
  -- Identity & configuration
  identity = string,                                -- Recipe identity ("vendor.lib@v1") - recipes only, not manifests
  options = table,                                  -- Recipe options (always present, may be empty)

  -- Directories (read-only paths)
  tmp_dir = string,                                 -- Ephemeral temp directory for ctx.fetch() downloads

  -- Download functions (concurrent, atomic commit)
  fetch = function(spec) -> string | table,         -- Download file(s), verify SHA256 if provided
                                                    -- spec: {url="...", sha256="..."} or {{...}, {...}}
                                                    -- Returns: basename(s) of downloaded file(s)

  -- Dependency access
  asset = function(identity) -> string,             -- Path to installed dependency asset

  -- Process execution
  run = function(cmd, ...) -> number,               -- Execute subprocess, stream output to logs
  run_capture = function(cmd, ...) -> table,        -- Capture stdout/stderr/exitcode
}

-- Platform globals (available everywhere in Lua)
ENVY_PLATFORM       -- "darwin", "linux", "windows"
ENVY_ARCH           -- "arm64", "x86_64", etc.
ENVY_PLATFORM_ARCH  -- "darwin-arm64", etc.
```

**Fetch function behavior:**
- **Single file**: `ctx.fetch({url = "...", sha256 = "..."})` → returns basename string
- **Multiple files**: `ctx.fetch({{url = "..."}, {url = "..."}})` → returns array of basenames
- **Concurrent**: All downloads happen in parallel via TBB task_group
- **Atomic**: All files downloaded and verified before ANY are committed to fetch_dir
- **Error handling**: If any download or verification fails, entire operation rolls back

**Example usage:**
```lua
function fetch(ctx)
  -- Single file
  local archive = ctx.fetch({url = "https://foo.com/gcc.tar.gz", sha256 = "abc..."})
  -- archive = "gcc.tar.gz"

  -- Multiple files (concurrent)
  local files = ctx.fetch({
    {url = "https://foo.com/gcc.tar.gz", sha256 = "abc..."},
    {url = "https://foo.com/gcc.tar.gz.sig"}  -- No SHA256, still downloaded
  })
  -- files = {"gcc.tar.gz", "gcc.tar.gz.sig"}

  -- Optional: verify signature using downloaded files
  -- (both files are now in ctx.tmp_dir directory)
end
```

**SHA256 verification:** If `sha256` field present, Envy computes SHA256 after download and errors if mismatch. If absent, download proceeds without verification (permissive mode).

### Cache Layout

**Recipe cache** (custom fetch produces multi-file directory):
```
~/.cache/envy/recipes/
└── corporate.toolchain@v1/
    ├── envy-complete         # Marker: recipe fetch complete
    ├── recipe.lua            # Entry point (required)
    ├── helpers.lua           # Additional files from ctx.fetch()
    ├── fetch/                # Downloaded files moved here after verification
    │   └── envy-complete
    └── work/
        └── tmp/              # Temp directory for ctx.fetch() (cleaned after)
```

## Dependency Function Semantics

**Dynamic dependencies** (recipe file):
```lua
dependencies = function(ctx)
  if ctx.options.enable_ssl then
    return { { recipe = "openssl.lib@v3", source = "...", sha256 = "..." } }
  end
  return {}
end
```

**Context:** Read-only `ctx` with `options`, `platform`, `arch`. No filesystem, network, or global mutation allowed. Exists solely to construct dependency table.

**Determinism:** Returned tables must be deterministic for given `(identity, options)`. Envy copies table before spawning child tasks to prevent accidental mutation.

**Option rewriting:** Parent recipes may transform child options:
```lua
dependencies = function(ctx)
  return {
    { recipe = "vendor.lib@v1", options = { version = ctx.options.lib_version or "2.0" } }
  }
end
```

Enables batteries-included bundles without manifest involvement.

## Policy Enforcement

**Security boundary:** Non-local recipes cannot depend on `local.*` recipes. Envy enforces after dependency evaluation—errors immediately if violation detected.

**Duplicate dependencies:** Same `(recipe, options, source)` in dependency list collapses to single node (deep comparison). Conflicting sources (same recipe/options, different source/sha256) surface as hard error.

**Local recipes:** Cannot use `fetch` function (parse-time error). Must use `file` path. Never cached.

## Error Propagation

**Graph construction errors:** Cycle detection, security violations, conflicting sources → immediate error with diagnostic (identity, cycle path, conflicting fields).

**Execution errors:** Recipe fetch failures, SHA256 mismatches, Lua errors, verb failures → node marks failed, propagates to dependents. Graph continues executing independent branches; final status reports all failures.

**Diagnostics:** Record identity, phase, error message. Higher layers map into TUI progress bars and CLI summary.

## Example Walkthrough

### Manifest
```lua
-- project/envy.lua
packages = {
  {
    recipe = "corporate.toolchain@v1",
    fetch = function(ctx)
      local jfrog = ctx:asset("jfrog.cli@v2")
      local tmp = ctx.tmp_dir
      ctx:run(jfrog .. "/bin/jfrog", "rt", "download", "recipes/toolchain.lua", tmp .. "/recipe.lua")
      ctx:commit_fetch({filename = "recipe.lua", sha256 = "abc123..."})
    end,
    dependencies = {
      { recipe = "jfrog.cli@v2", source = "https://public.com/jfrog.lua", sha256 = "def456...", needed_by = "recipe_fetch" }
    }
  },
  "vendor.library@v1"  -- Shorthand, no custom fetch
}
```

### Recipe Files

**jfrog.cli@v2 (simple declarative recipe):**
```lua
-- Fetched from https://public.com/jfrog.lua
fetch = { url = "https://jfrog.com/cli/jfrog-cli.tar.gz", sha256 = "789abc..." }
install = function(ctx)
  ctx:extract_all()
  ctx:add_to_path("bin")
end
```

**corporate.toolchain@v1 (dynamically loaded after jfrog.cli installs):**
```lua
-- Fetched via custom function using jfrog CLI
dependencies = function(ctx)
  return {
    { recipe = "vendor.compiler@v3", source = "...", sha256 = "..." },
    { recipe = "vendor.linker@v2", source = "...", sha256 = "..." }
  }
end

build = function(ctx)
  local compiler = ctx:asset("vendor.compiler@v3")
  ctx:run(compiler .. "/bin/gcc", "-o", ctx.install_dir() .. "/toolchain", "main.c")
end
```

**vendor.library@v1 (simple archive):**
```lua
-- Fetched from default source in manifest
fetch = { url = "https://vendor.com/lib.tar.gz", sha256 = "ghi789..." }
stage = function(ctx) ctx:extract_all() end
install = function(ctx)
  ctx:run("cp", "-r", ctx.stage_dir() .. "/include", ctx.install_dir() .. "/include")
end
```

### Execution Timeline

**Initial state:** Cache empty, no recipes or assets installed.

**Graph construction:**
1. Command seeds graph with `corporate.toolchain@v1` and `vendor.library@v1` recipe_fetch nodes
2. Broadcast kickoff triggers parallel execution

**Phase 1: Bootstrap JFrog (parallel with vendor.library):**
3. `jfrog.cli@v2.recipe_fetch` executes (no dependencies, starts immediately)
   - Download https://public.com/jfrog.lua
   - Verify SHA256 "def456..."
   - Cache at `~/.cache/envy/recipes/jfrog.cli@v2.lua`
   - Load Lua, discover no dependencies
   - Complete `recipe_fetch`, unblock `fetch` phase
4. `jfrog.cli@v2.fetch` executes
   - Download https://jfrog.com/cli/jfrog-cli.tar.gz
   - Verify SHA256 "789abc..."
   - Extract to `fetch/`, mark fetch complete
5. `jfrog.cli@v2.install` executes (no stage/build phases declared, node optimization skips them)
   - Extract archive to install dir
   - Add bin/ to PATH metadata
   - Atomic rename `install/` → `asset/`
   - Mark complete
6. `jfrog.cli@v2.deploy` completes (no deploy verb, auto-completes immediately)

**Phase 2: Corporate toolchain (blocked on jfrog.cli until step 6):**
7. `corporate.toolchain@v1.recipe_fetch` executes (unblocked after jfrog.cli.deploy)
   - Create workspace `/tmp/envy-corporate.toolchain-v1-xyz/`
   - Call fetch function: `ctx:asset("jfrog.cli@v2")` returns `/cache/assets/jfrog.cli@v2/.../asset`
   - Run jfrog CLI: download toolchain.lua to workspace
   - `ctx:import_file(workspace/toolchain.lua, recipe.lua, "abc123...")` verifies SHA256, copies to cache
   - Load Lua from `~/.cache/envy/recipes/corporate.toolchain@v1/recipe.lua`
   - Evaluate `dependencies` function → returns compiler, linker
   - Add `vendor.compiler@v3` and `vendor.linker@v2` nodes to graph with edges
   - Complete `recipe_fetch`

**Phase 3: Compiler/linker (parallel, new nodes added during step 7):**
8. `vendor.compiler@v3.recipe_fetch`, `vendor.linker@v2.recipe_fetch` execute in parallel
   - Fetch recipes from URLs, verify, cache, load Lua
9. `vendor.compiler@v3` fetch/install phases execute
10. `vendor.linker@v2` fetch/install phases execute

**Phase 4: Toolchain build (blocked on compiler/linker):**
11. `corporate.toolchain@v1.build` executes (unblocked after compiler.deploy)
    - `ctx:asset("vendor.compiler@v3")` returns compiler path
    - Compile toolchain using vendor compiler
12. `corporate.toolchain@v1.install` executes
13. `corporate.toolchain@v1.deploy` completes

**Phase 5: Vendor library (independent, ran in parallel with jfrog bootstrap):**
14. `vendor.library@v1.recipe_fetch` executed early (step 3, no dependencies)
15. `vendor.library@v1` fetch/stage/install phases executed (independent of toolchain)

**Final state:** All nodes complete. Graph topology preserved for debugging/summary. Asset cache populated with jfrog.cli, compiler, linker, toolchain, library.

### Graph Topology

```
[jfrog.cli@v2 recipe_fetch] → [jfrog.cli@v2 fetch] → [jfrog.cli@v2 install] → [jfrog.cli@v2 deploy]
                                                                                      ↓
                                                     [corporate.toolchain@v1 recipe_fetch] → [corporate.toolchain@v1 build] → [corporate.toolchain@v1 install]
                                                                                                       ↓                                   ↓
                                                                                  [vendor.compiler@v3 recipe_fetch] → ... → [vendor.compiler@v3 deploy]
                                                                                  [vendor.linker@v2 recipe_fetch] → ... → [vendor.linker@v2 deploy]

[vendor.library@v1 recipe_fetch] → [vendor.library@v1 fetch] → [vendor.library@v1 stage] → [vendor.library@v1 install]
```

Nodes execute when predecessors complete. TBB scheduler maximizes parallelism within topological constraints.

## Command Integration

**Single entry point:**
```cpp
bool cmd_install::execute() {
  unified_dag dag{ cache_, manifest_->packages() };
  dag.execute();  // Builds graph, seeds with manifest roots, waits for completion
  print_summary(dag.roots());
  return true;
}
```

**Implementation sketch:**
```cpp
class unified_dag {
  tbb::flow::graph g_;
  cache& cache_;
  concurrent_hash_map<string, dag_node*> nodes_;  // Memoization

  dag_node* ensure_node(recipe::cfg const& cfg) {
    string key = canonical_key(cfg);
    if (auto [it, inserted] = nodes_.insert({key, nullptr}); inserted) {
      it->second = new dag_node{cfg, create_phase_nodes(g_, cfg)};
    }
    return nodes_[key];
  }

  void execute() {
    // Seed graph with manifest roots
    tbb::flow::broadcast_node<msg> kickoff{g_};
    for (auto& pkg : manifest_packages_) {
      dag_node* node = ensure_node(pkg);
      tbb::flow::make_edge(kickoff, node->recipe_fetch);
    }

    // Execute (graph expands dynamically as recipe_fetch nodes discover deps)
    kickoff.try_put(msg{});
    g_.wait_for_all();
  }
};
```

`recipe_fetch` node body calls `ensure_node()` for each dependency, adds edges based on `needed_by`. Graph grows until all transitive dependencies discovered.

## Future Enhancements

**Offline DAG caching:** Memoization keys enable serializing resolved graph to disk. Subsequent runs skip recipe fetching if cache valid (check recipe SHA256s). Load graph from cache, execute asset phases only.

**Progress tracking:** Each phase node reports progress (0-100%) via TUI handle. User sees: "Fetching jfrog.cli@v2 [=====> ] 45%", "Building corporate.toolchain@v1 [========>] 80%".

**Partial execution:** Skip phases for already-installed assets (`envy-complete` marker present). Graph prunes completed nodes before execution. Supports incremental updates.

**Speculative fetching:** Recipe fetch can preemptively download common dependencies before Lua evaluation completes. Reduces critical path latency for well-known recipes.

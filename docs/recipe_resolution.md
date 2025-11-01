# Recipe Resolution

## Core Model
- **Identity**: `namespace.name@version` plus optional SHA256 for remote/bundled sources.
- **Instantiation**: `(identity, options)` uniquely describes a graph node; options originate from manifests or parent recipes.
- **Memoization key**: Canonical string `"identity{key1=val1,key2=val2}"` with options sorted lexicographically. Empty options omit braces. String keys enable trivial hashing, debuggability, and future serialization.
- **Dependencies field**: Recipe may define `dependencies = { ... }` (static table) or `dependencies = function(ctx) ... end` (dynamic). Both forms normalize to a plain Lua table before recursion continues.
- **Recipe object**: Resolved node carrying `recipe::cfg` (identity, source, options), `lua_state_ptr` (for verb execution), and dependency pointers. Each recipe owns its Lua state; verbs (`check`, `fetch`, `stage`, `build`, `install`) query this state at execution time.

## Resolver Contract
1. Determine source: remote (fetch+verify via cache using sha256 key), local (direct load from filesystem).
2. Load Lua chunk once per `(identity, options)`; create `recipe` object with cfg + lua_state.
3. Evaluate `dependencies` field producing plain table. Static tables copied; functions receive read-only `ctx` (options, platform, arch). Parse each dependency via `recipe::cfg::parse`.
4. Schedule child resolutions via task_group; parent waits after spawning all children.
5. Assemble resolved `recipe` object (cfg, lua_state, dependency pointers) when children complete; publish to shared map keyed by canonical string.

## Concurrency & Memoization
- `unordered_map<string, shared_ptr<promise<recipe>>>` tracks in-flight nodes keyed by canonical string. First thread allocates promise, runs resolution, fulfills with `shared_ptr<recipe>`; later threads wait on shared future.
- Recursion uses `task_group.run()` for depth-first parallelism. Fetch latency and Lua execution overlap naturally across independent branches.
- Each resolved `recipe` is a `shared_ptr` enabling safe sharing across the DAG without copies. Installation phase references these same objects for verb execution.

## Dependency Function Semantics
- `ctx` is immutable; no filesystem, network, or global mutation allowed. It exists solely to read options and construct child tables.
- Returned tables must be deterministic for a given `(identity, options)`; callers copy the table before spawning tasks to prevent accidental mutation.
- Parent recipes may rewrite child options before returning the table, enabling batteries-included bundles without manifest involvement.

## Cycle & Policy Enforcement
- Each resolver maintains a thread-local stack of active `(identity, options)` keys. Encountering an unfinished entry signals a cycle; the promise stores an error with the discovered path.
- Non-local recipes depending on `local.*` recipes are rejected immediately after dependency evaluation.
- Duplicate dependency entries collapse via deep comparison of `(recipe, options, source)`; conflicting sources surface as hard errors.

## Error Propagation
- Errors aggregate inside the promise; parents collect child failures and rethrow a summarized exception. The resolver never hides partial graphs.
- Diagnostics record identity, dependency, optional cycle path, and a terse message; higher layers map these into CLI output or TUI panes.

## Verb Execution
- Verbs remain in the preserved `lua_state` sandbox; when the executor needs a verb it inspects the table by name, determines whether it is a string (built-in helper) or function, and dispatches accordingly.
- **`check`** verb (optional): Runs before any installation work. String form executes as shell command (exit 0 = satisfied); function form returns boolean. If absent, checks cache marker (`.envy-complete`). Enables wrapping system package managers without cache involvement.
- String verbs resolve to built-in handlers at call time; function verbs receive a `verb_ctx` exposing execution helpers (extract, cache access, process launching, dependency asset access).

## Workspace Phases
- **Fetch** populates the durable workspace root (`assets/<entry>/.work/fetch/`) and may be specified as a table (declarative archive/git download) or function (custom logic). Skipping the table and leaving the verb undefined triggers Envy’s default.
- **Stage** prepares the build staging area (`assets/<entry>/.work/stage/`). By default Envy unpacks archives into this directory and deep-copies (or reflinks) git checkouts so the durable fetch tree stays pristine. Declarative tables tweak the behaviour (copy mode, strip rules, etc.); functions receive both fetch and stage directories. Authors may set `fetch_into_work_dir=true` to opt out of reuse—Envy fetches directly into the stage directory and wipes it between runs.
- **Build** runs toolchains against the staging directory. Envy guarantees the staging directory starts empty each attempt and the install directory (`assets/<entry>/.install/`) is ready to receive build outputs.
- **Install** writes final artifacts into `.install/`; once `mark_complete()` fires, Envy renames `.install/` to `asset/`, fingerprints the payload, deletes the workspace, and finally touches `.envy-complete`.
- The runtime exposes `ctx.fetch_dir()`, `ctx.staging_dir()`, and `ctx.install_dir()` to Lua so recipes can orchestrate each phase without managing cache bookkeeping directly.

## Example Walkthrough

### Manifest & Recipes
```lua
-- project/envy.lua
packages = {
  {
    recipe = "vendor.toolchain@v1",
    url = "https://example.com/toolchain.lua",
    sha256 = "1111...aaaa",
    options = { variant = "full", arch = "x86_64" },
  },
  "local.cli@v1",
}
```

Key recipes (all previously uncached):
- `vendor.toolchain@v1` (remote): `dependencies = function(ctx)` returns compiler/runtime/tools based on `ctx.options.variant`.
- `vendor.compiler@v3` (remote): `dependencies = { "vendor.binutils@v2" }`.
- `vendor.runtime@v2` (remote): dynamic dependency on `vendor.zlib@v1` when `ctx.options.enable_zlib` flag is true (default).
- `vendor.tools@v1` (remote archive): static table of helper utilities.
- `local.cli@v1` (project-local): depends on `"vendor.toolchain@v1"` and `"local.shared@v1"`.
- `local.shared@v1` (project-local): no dependencies.

### Timeline (cache initially empty)
1. Root task submits `resolve("vendor.toolchain@v1", {variant="full",arch="x86_64"})` and `resolve("local.cli@v1", {})`.
2. Toolchain resolver acquires promise slot, downloads `toolchain.lua` into `~/.cache/envy/recipes/vendor.toolchain@v1.lua`, loads Lua, evaluates `dependencies(ctx)` → `{ compiler@v3(opt), runtime@v2(opt), tools@v1 }`.
3. Resolver spawns three child tasks concurrently: compiler, runtime, tools.
4. Compiler task fetches `compiler.lua`, caches it, evaluates dependencies (static table), spawns `vendor.binutils@v2`.
5. Binutils fetches source, caches it, has no children → promise fulfills.
6. Runtime task fetches `runtime.lua`, runs dynamic dependencies (returns `{ vendor.zlib@v1 }`), spawns zlib.
7. Zlib fetches remote script, caches file, no children → fulfills; runtime promise resolves.
8. Tools task downloads archive recipe, caches directory, static dependencies empty → resolves.
9. Toolchain waits for three futures, records pointers, fulfills promise with retained Lua state.
10. `local.cli@v1` resolver loads local script, dependencies table = `{ vendor.toolchain@v1, local.shared@v1 }`, reuses existing toolchain future (no re-fetch), spawns shared resolver.
11. `local.shared@v1` loads project file, no deps → fulfills.
12. CLI waits, stores child pointers, fulfills. All promises resolved; DAG ready.

### Runtime Data Structures
- `promise_map`: `unordered_map<string, shared_ptr<promise<recipe>>>` holding in-flight nodes; canonical string keys cover both manifest roots and transitive entries.
- `task_group`: single group managing all resolution tasks across entire DAG.
- `recipe`: resolved node containing `cfg` (identity, source, options), `lua_state_ptr` (verb execution sandbox), and `vector<shared_ptr<recipe>>` dependencies.
- Output: `vector<shared_ptr<recipe>>` of manifest roots; full DAG reachable via dependency traversal. Installation phase uses these shared pointers directly.

### Final Resolved Graph
```
resolved_graph["vendor.toolchain@v1{variant=full,arch=x86_64}"] = {
  deps = [
    -> "vendor.compiler@v3{variant=full,arch=x86_64}",
    -> "vendor.runtime@v2{enable_zlib=true}",
    -> "vendor.tools@v1{}"
  ]
}
resolved_graph["vendor.compiler@v3{variant=full,arch=x86_64}"] = {
  deps = [ -> "vendor.binutils@v2{}" ]
}
resolved_graph["vendor.runtime@v2{enable_zlib=true}"] = {
  deps = [ -> "vendor.zlib@v1{}" ]
}
resolved_graph["local.cli@v1{}"] = {
  deps = [
    -> "vendor.toolchain@v1{variant=full,arch=x86_64}",
    -> "local.shared@v1{}"
  ]
}
```

### oneTBB Execution View

Resolution fan-out (tasks spawn left-to-right; dashed edges reuse existing futures):
```
        [vendor.toolchain@v1]
          /      |        \
         /       |         \
 [vendor.compiler] [vendor.runtime] [vendor.tools]
        |              |
 [vendor.binutils] [vendor.zlib]

[local.cli] ----reuse----> [vendor.toolchain]
     |
[local.shared]
```

Recipe execution phase (verbs scheduled after resolution completes):
```
           exec:toolchain(fetch→stage→build→install)
             /                   |                 \
 exec:compiler           exec:runtime       exec:tools
        |                 |
exec:binutils      exec:zlib

exec:cli --depends--> exec:toolchain
    |
exec:shared
```

## Command Integration

Commands invoke resolution via blocking API:

```cpp
std::vector<std::shared_ptr<recipe>> resolve_recipes(
  std::vector<recipe::cfg> const& packages,
  cache& c
);
```

Returns manifest root recipes; full DAG reachable via `recipe::dependencies()`. Implementation uses `task_group` for parallel fork-join resolution. Commands run inside `task_arena` established by main—resolver shares that thread pool. TBB's work-stealing scheduler handles nested blocking without deadlock.

After resolution, commands invoke blocking verb execution helper:

```cpp
void ensure_assets(std::vector<std::shared_ptr<recipe>> const& roots);
```

Constructs `flow::graph` modeling verb dependencies (fetch→stage→build→install per recipe, with inter-recipe edges from `recipe::dependencies()`), executes via `wait_for_all()`. Each node queries its `recipe` object's Lua state for verb implementations at execution time.

**Pattern:**
```cpp
bool cmd_install::execute() {
  auto roots{ resolve_recipes(manifest_.packages, cache_) };  // Parallel
  ensure_assets(roots);                                        // Parallel
  print_summary(roots);                                        // Sequential
  return true;
}
```

## Future Hooks
- Deterministic memo entries enable offline caching of resolution DAGs.
- Shared futures provide a single choke point for tracing, metrics, or progress notifications without touching Lua.

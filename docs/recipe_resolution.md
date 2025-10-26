# Recipe Resolution

## Core Model
- **Identity**: `namespace.name@version` plus optional SHA256 for remote/bundled sources.
- **Instantiation**: `(identity, options)` uniquely describes a graph node; options originate from manifests or parent recipes.
- **Dependencies field**: Recipe may define `dependencies = { ... }` (static table) or `dependencies = function(ctx) ... end` (dynamic). Both forms normalize to a plain Lua table before recursion continues.

## Resolver Contract
1. Determine source via manifest overrides, recipe-local overrides, or built-in tables.
2. Fetch (or reuse cached) recipe source; load Lua chunk once per `(identity, options)` using a memo locked by that key.
3. Evaluate the `dependencies` field, producing a fresh table. Static tables are copied; functions receive a read-only `ctx` exposing `ctx.options`, `ctx.platform`, `ctx.arch`, and helper shims (e.g., `ctx:manifest_option("zstd")`).
4. Schedule child resolutions immediately using oneTBB task groups; parents wait only after spawning every child.
5. Assemble and publish the resolved node (dependency pointers plus the retained `lua_state`) when all child futures complete; propagate any accumulated errors to callers. Verbs stay inside that sandbox and are queried later during execution.

## Concurrency & Memoization
- `tbb::concurrent_hash_map<resolved_key, std::shared_ptr<promise>>` tracks in-flight nodes. First thread allocates the promise, runs resolution, and fulfills or rejects it; later threads wait on the shared future.
- Recursion uses `task_group.run()` to exploit depth-first parallelism while preserving determinism. Fetch latency and Lua execution overlap naturally.
- Dependency tables cache inside the promise payload to avoid re-running Lua after the initial evaluation.

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

## Verb Normalization
- Verbs remain in the preserved `lua_state` sandbox; when the executor needs a verb it inspects the table by name, determines whether it is a string (built-in helper) or function, and dispatches accordingly.
- String verbs resolve to built-in handlers at call time; function verbs receive a `verb_ctx` mirroring the dependency `ctx` but exposing execution helpers (extract, cache access, process launching).

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

overrides = {
  ["vendor.compiler@v3"] = {
    url = "https://mirror.example/compiler.lua",
    sha256 = "2222...bbbb",
  },
  ["vendor.runtime@v2"] = {
    file = "./envy/recipes/runtime.lua",  -- direct project override
  },
}
```

Key recipes (all previously uncached):
- `vendor.toolchain@v1` (remote): `dependencies = function(ctx)` returns compiler/runtime/tools based on `ctx.options.variant`. Emits tool overrides for `"full"`.
- `vendor.compiler@v3` (remote, overridden URL): `dependencies = { "vendor.binutils@v2" }`.
- `vendor.runtime@v2` (project-local override): dynamic dependency on `vendor.zlib@v1` when `ctx.options.enable_zlib` flag is true (default).
- `vendor.tools@v1` (remote archive): static table of helper utilities.
- `local.cli@v1` (project-local): depends on `"vendor.toolchain@v1"` and `"local.shared@v1"`.
- `local.shared@v1` (project-local): no dependencies.

### Timeline (cache initially empty)
1. Root task submits `resolve("vendor.toolchain@v1", {variant="full",arch="x86_64"})` and `resolve("local.cli@v1", {})`.
2. Toolchain resolver acquires promise slot, downloads `toolchain.lua` into `~/.cache/envy/recipes/vendor.toolchain@v1.lua`, loads Lua, evaluates `dependencies(ctx)` → `{ compiler@v3(opt), runtime@v2(opt), tools@v1 }`.
3. Resolver spawns three child tasks concurrently: compiler, runtime, tools.
4. Compiler task hits manifest override, fetches mirrored `compiler.lua`, caches it, evaluates dependencies (static table), spawns `vendor.binutils@v2`.
5. Binutils fetches original source, caches archive, has no children → promise fulfills.
6. Runtime task detects local override, loads `./envy/recipes/runtime.lua` without touching cache, runs dynamic dependencies (returns `{ vendor.zlib@v1 }`), spawns zlib.
7. Zlib fetches remote script, caches file, no children → fulfills; runtime promise resolves.
8. Tools task downloads archive recipe, caches directory, static dependencies empty → resolves.
9. Toolchain waits for three futures, records pointers, fulfills promise with retained Lua state.
10. `local.cli@v1` resolver loads local script, dependencies table = `{ vendor.toolchain@v1, local.shared@v1 }`, reuses existing toolchain future (no re-fetch), spawns shared resolver.
11. `local.shared@v1` loads project file, no deps → fulfills.
12. CLI waits, stores child pointers, fulfills. All promises resolved; DAG ready.

### Runtime Data Structures
- `promise_map`: `concurrent_hash_map<resolved_key, shared_ptr<promise>>` holding in-flight nodes; keys include both manifest roots and transitive entries.
- `task_group`: per-root recursion group managing outstanding TBB tasks.
- `dependency_cache`: stored inside each promise payload (copy of normalized dependency table).
- `resolved_graph`: `unordered_map<resolved_key, resolved_recipe>` published on promise fulfillment; each entry stores identity, options, child pointers, and the `lua_state`.

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

## Future Hooks
- Deterministic memo entries enable offline caching of resolution DAGs.
- Shared futures provide a single choke point for tracing, metrics, or progress notifications without touching Lua.

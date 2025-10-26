# Recipe Resolution Implementation Guide

## Overview

Recipe resolution transforms a project manifest into an executable dependency graph. Two phases: (1) parallel recipe acquisition via TBB, (2) serial graph construction via DFS. Input: manifest with package list and overrides. Output: resolved recipe DAG with validated dependencies, ready for verb execution.

## Core Data Structures

### Recipe Identity vs. Instantiation

**Recipe identity**: `(namespace.name@version, sha256)` - immutable source code, cacheable
**Recipe instantiation**: `(identity, options)` - parameterized execution context, graph node

Two recipes with same `(identity, sha256)` are byte-identical regardless of URL provenance. Multiple instantiations of same recipe with different options create distinct graph nodes, distinct cache entries, distinct deployments.

### Keys and Caches

```cpp
// Recipe identity (source)
struct recipe_key {
  std::string identity;     // "arm.gcc@v2"
  std::string sha256;       // Hash of recipe Lua source
};

// Recipe instantiation (graph node)
struct resolved_key {
  std::string identity;     // "arm.gcc@v2"
  options_map options;      // { version: "13.2.0", target: "arm-none-eabi" }
};

// Phase 1: Recipe source cache
tbb::concurrent_hash_map<recipe_key, fs::path> recipe_cache;
tbb::concurrent_set<resolved_key> seen;

// Phase 2: Resolved recipe graph
std::map<resolved_key, resolved_recipe> visited;
std::set<resolved_key> visiting;  // Cycle detection
std::vector<resolution_error> errors;
```

### Resolved Recipe

```cpp
struct resolved_recipe {
  std::string identity;              // "arm.gcc@v2"
  options_map options;               // Evaluated options for this instantiation
  std::vector<std::string> verbs;    // Implemented verbs: ["fetch", "build", "install"]
  std::vector<dependency> deps;      // Resolved dependencies
  lua_recipe_object recipe_obj;      // Loaded Lua recipe (holds functions)
};

struct dependency {
  resolved_recipe* target;           // Pointer into visited map
  std::optional<std::string> needed_by;  // Which verb needs this dep complete
};
```

## Phase 1: Parallel Recipe Acquisition

### Objective

Fetch all recipes (root + transitive deps) into filesystem cache. Maximize parallelism: different recipes fetch concurrently. Handle cache contention: `cache::ensure_recipe` serializes per-recipe via locks.

### Algorithm

```cpp
void fetch_recipes(
    const package_ref& pkg,
    const override_map& overrides,
    tbb::task_group& task_group) {

  resolved_key key{pkg.identity, pkg.options};

  // Atomic insert - first caller proceeds, duplicates bail
  if (!seen.insert(key).second) {
    return;
  }

  // Spawn TBB task for this recipe
  task_group.run([=, &task_group] {
    // Apply manifest + parent overrides to determine source
    package_source src = apply_overrides(pkg, overrides);
    recipe_key rkey{pkg.identity, src.sha256};

    // Fetch recipe (may block on cache lock, or instant hit)
    fs::path recipe_path;
    if (src.file) {
      // Local recipe: load directly from project tree
      recipe_path = project_root / src.file;
    } else {
      // Remote recipe: ensure cached
      recipe_path = cache::ensure_recipe(pkg.identity, src.sha256, src.url);
    }

    // Store in concurrent map
    recipe_cache.insert(rkey, recipe_path);

    // Execute recipe in fresh lua_state to discover dependencies
    lua_State* L = create_lua_state();
    load_envy_globals(L);  // ENVY_PLATFORM, ENVY_ARCH, etc.
    lua_recipe_object recipe = execute_recipe(L, recipe_path);

    // Spawn tasks for transitive dependencies
    if (recipe.has_make_depends) {
      override_map child_overrides = merge_overrides(overrides, recipe.overrides);
      std::vector<package_ref> deps = recipe.make_depends(L, pkg.options);

      for (const auto& dep : deps) {
        fetch_recipes(dep, child_overrides, task_group);
      }
    }

    lua_close(L);
  });
}

// Entry point
tbb::task_group g;
for (const auto& pkg : manifest.packages) {
  fetch_recipes(pkg, manifest.overrides, g);
}
g.wait();  // Block until all recipes fetched
```

### Key Points

**Concurrency**: Each `fetch_recipes` call spawns immediately. No waiting for parents to complete. TBB scheduler manages thread pool.

**Deduplication**: `seen` set (concurrent) prevents duplicate work. First task to insert `(identity, options)` proceeds; others skip.

**Cache locks**: `cache::ensure_recipe` acquires exclusive lock if cache miss. Other tasks requesting same recipe block on lock, then recheck `.envy-complete` marker (instant hit after first completes).

**Local recipes**: Never cached in `~/.cache/envy/`. Loaded directly from project tree. Still inserted into `recipe_cache` map with project-relative path for phase 2 lookup.

**Override propagation**: Each recipe merges parent overrides with its own. Children see accumulated override map.

**Lua isolation**: Each `execute_recipe` call gets fresh `lua_State`. Prevents threading races, state contamination between recipes.

## Phase 2: Serial Dependency Resolution

### Objective

Build validated dependency graph. Check cycles, verify `needed_by` references, accumulate errors. Fast (milliseconds) since all recipes cached.

### Algorithm

```cpp
resolved_recipe* resolve_recipe(
    const package_ref& pkg,
    const override_map& overrides) {

  resolved_key key{pkg.identity, pkg.options};

  // Check if already resolved
  if (auto it = visited.find(key); it != visited.end()) {
    return &it->second;
  }

  // Cycle detection: back-edge in DFS
  if (visiting.contains(key)) {
    errors.push_back({
      .type = error_type::cycle,
      .message = fmt::format("Dependency cycle detected at {}", key.identity),
      .path = build_cycle_path(visiting, key)
    });
    return nullptr;
  }
  visiting.insert(key);

  // Determine source (same logic as phase 1)
  package_source src = apply_overrides(pkg, overrides);
  recipe_key rkey{pkg.identity, src.sha256};

  // Lookup cached recipe path (guaranteed to exist after phase 1)
  auto cache_it = recipe_cache.find(rkey);
  if (cache_it == recipe_cache.end()) {
    errors.push_back({
      .type = error_type::missing_recipe,
      .message = fmt::format("Recipe {} not in cache (phase 1 failure)", key.identity)
    });
    visiting.erase(key);
    return nullptr;
  }
  fs::path recipe_path = cache_it->second;

  // Load and execute recipe
  lua_State* L = create_lua_state();
  load_envy_globals(L);
  lua_recipe_object recipe_obj = execute_recipe(L, recipe_path);

  // Introspect implemented verbs
  std::vector<std::string> verbs = introspect_verbs(recipe_obj);
  // introspect_verbs checks for functions: fetch, build, install, deploy, check

  // Resolve dependencies recursively
  std::vector<dependency> deps;
  if (recipe_obj.has_make_depends) {
    override_map child_overrides = merge_overrides(overrides, recipe_obj.overrides);
    std::vector<package_ref> raw_deps = recipe_obj.make_depends(L, pkg.options);

    for (const auto& dep_pkg : raw_deps) {
      // Validate needed_by if present
      if (dep_pkg.needed_by) {
        if (std::find(verbs.begin(), verbs.end(), *dep_pkg.needed_by) == verbs.end()) {
          errors.push_back({
            .type = error_type::invalid_needed_by,
            .message = fmt::format(
              "Recipe {} declares needed_by='{}' for dependency {} but has no {} verb",
              key.identity, *dep_pkg.needed_by, dep_pkg.identity, *dep_pkg.needed_by),
            .recipe = key.identity,
            .dependency = dep_pkg.identity
          });
          // Continue resolving to collect more errors
        }
      }

      // Recursively resolve dependency
      resolved_recipe* resolved_dep = resolve_recipe(dep_pkg, child_overrides);
      if (resolved_dep) {
        deps.push_back({
          .target = resolved_dep,
          .needed_by = dep_pkg.needed_by
        });
      }
    }
  }

  // Remove from visiting set (DFS backtrack)
  visiting.erase(key);

  // Insert into visited map
  resolved_recipe resolved{
    .identity = key.identity,
    .options = key.options,
    .verbs = verbs,
    .deps = deps,
    .recipe_obj = recipe_obj
  };

  auto [it, inserted] = visited.insert({key, std::move(resolved)});
  lua_close(L);

  return &it->second;
}

// Entry point
for (const auto& pkg : manifest.packages) {
  resolve_recipe(pkg, manifest.overrides);
}

// Check for errors after full traversal
if (!errors.empty()) {
  report_resolution_errors(errors);
  return std::unexpected(resolution_failed);
}
```

### Key Points

**Memoization**: `visited` map prevents redundant resolution. If recipe A and B both depend on C with identical options, C resolved once, both A and B reference same entry.

**Cycle detection**: `visiting` set tracks current DFS path. Revisiting node in `visiting` = cycle detected. Collect error, continue resolution to find more issues.

**Error accumulation**: Don't fail-fast. Collect all errors (cycles, invalid `needed_by`, missing recipes) and report batch. Allows user to fix multiple issues in one iteration.

**Validation**: Check `needed_by` field references implemented verb. Recipe declares `needed_by = "build"` but only implements `fetch` and `install` → error.

**Null handling**: If dependency resolution returns `nullptr` (cycle or error), don't add to `deps` list but continue processing other deps. Graph construction best-effort for error reporting.

**Lua state cleanup**: Close `lua_State` after extracting recipe data. Each resolution gets fresh VM.

## Override Application

### Precedence Rules

1. **Manifest overrides** trump all (highest priority)
2. **Recipe overrides** apply to dependencies
3. **Package declaration** (lowest priority)

### Algorithm

```cpp
package_source apply_overrides(
    const package_ref& pkg,
    const override_map& overrides) {

  // Check manifest overrides first
  if (auto it = overrides.find(pkg.identity); it != overrides.end()) {
    return {
      .url = it->second.url,
      .sha256 = it->second.sha256,
      .file = it->second.file
    };
  }

  // Fall back to package declaration
  return {
    .url = pkg.url,
    .sha256 = pkg.sha256,
    .file = pkg.file
  };
}

override_map merge_overrides(
    const override_map& parent,
    const override_map& recipe) {

  override_map result = parent;  // Start with parent overrides

  // Recipe overrides supplement parent (parent wins on conflict)
  for (const auto& [identity, override] : recipe) {
    result.try_emplace(identity, override);
  }

  return result;
}
```

**Semantics**: Manifest sets global policy. Recipe can add overrides for its dependencies, but cannot override what manifest already specified. Child recipes see accumulated overrides from all ancestors plus manifest.

## Verb Introspection

### Purpose

Determine which verbs a recipe implements. Needed for two validations:
1. Check `needed_by` references real verb
2. Build verb execution DAG (only create nodes for implemented verbs)

### Implementation

```cpp
std::vector<std::string> introspect_verbs(const lua_recipe_object& recipe) {
  std::vector<std::string> verbs;

  // Check for each standard verb function
  if (recipe.fetch_fn) verbs.push_back("fetch");
  if (recipe.build_fn) verbs.push_back("build");
  if (recipe.install_fn) verbs.push_back("install");
  if (recipe.deploy_fn) verbs.push_back("deploy");
  if (recipe.check_fn) verbs.push_back("check");

  return verbs;
}

// During recipe loading
lua_recipe_object execute_recipe(lua_State* L, const fs::path& path) {
  // Load and execute recipe script
  if (luaL_loadfile(L, path.c_str()) != LUA_OK) {
    throw recipe_load_error(lua_tostring(L, -1));
  }
  if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
    throw recipe_exec_error(lua_tostring(L, -1));
  }

  lua_recipe_object obj;

  // Extract identity
  lua_getglobal(L, "identity");
  if (lua_isstring(L, -1)) {
    obj.identity = lua_tostring(L, -1);
  } else {
    throw recipe_error("Recipe missing 'identity' string");
  }
  lua_pop(L, 1);

  // Check for verb functions
  lua_getglobal(L, "fetch");
  obj.fetch_fn = lua_isfunction(L, -1);
  lua_pop(L, 1);

  lua_getglobal(L, "build");
  obj.build_fn = lua_isfunction(L, -1);
  lua_pop(L, 1);

  lua_getglobal(L, "install");
  obj.install_fn = lua_isfunction(L, -1);
  lua_pop(L, 1);

  lua_getglobal(L, "deploy");
  obj.deploy_fn = lua_isfunction(L, -1);
  lua_pop(L, 1);

  lua_getglobal(L, "check");
  obj.check_fn = lua_isfunction(L, -1);
  lua_pop(L, 1);

  // Check for make_depends function
  lua_getglobal(L, "make_depends");
  obj.has_make_depends = lua_isfunction(L, -1);
  lua_pop(L, 1);

  // Extract overrides table if present
  lua_getglobal(L, "overrides");
  if (lua_istable(L, -1)) {
    obj.overrides = extract_override_map(L, -1);
  }
  lua_pop(L, 1);

  return obj;
}
```

## Complete Example

### Scenario

Project manifest declares two packages:
- `vendor.openocd@v3` - needs gcc to build
- `envy.jfrog@v2` - fetches artifacts from Artifactory

OpenOCD depends on `arm.gcc@v2` (needs gcc before building).
JFrog has no dependencies.

### Manifest

```lua
-- project/envy.lua
packages = {
  {
    recipe = "vendor.openocd@v3",
    url = "https://example.com/recipes/openocd-v3.lua",
    sha256 = "aabbccdd...",
    options = { version = "0.12.0" }
  },
  {
    recipe = "envy.jfrog@v2",
    options = { version = "2.50.0" }
  }
}

overrides = {
  ["arm.gcc@v2"] = {
    url = "https://internal-mirror.company/gcc-v2.lua",
    sha256 = "11223344..."
  }
}
```

### OpenOCD Recipe

```lua
-- openocd-v3.lua
identity = "vendor.openocd@v3"

function make_depends(options)
  return {
    {
      recipe = "arm.gcc@v2",
      url = "https://public.com/gcc-v2.lua",  -- Will be overridden by manifest
      sha256 = "11223344...",
      options = { version = "13.2.0" },
      needed_by = "build"
    }
  }
end

function fetch(options)
  return {
    url = "https://downloads.com/openocd-" .. options.version .. ".tar.gz",
    sha256 = "..."
  }
end

function build(ctx)
  local gcc_root = ctx.asset("arm.gcc@v2")
  ctx.run("./configure", "--prefix=" .. ctx.staging_dir,
          "CC=" .. gcc_root .. "/bin/arm-none-eabi-gcc")
  ctx.run("make", "-j" .. ctx.cores)
end

function install(ctx)
  ctx.run("make", "install")
end
```

### GCC Recipe

```lua
-- gcc-v2.lua (from internal mirror)
identity = "arm.gcc@v2"

function fetch(options)
  local version = options.version or "13.2.0"
  return {
    url = "https://arm.com/gcc-" .. version .. "-" .. ENVY_PLATFORM_ARCH .. ".tar.xz",
    sha256 = "..."
  }
end

function install(ctx)
  ctx.extract_all()
end
```

### JFrog Recipe

```lua
-- Built-in recipe (embedded in binary)
identity = "envy.jfrog@v2"

function fetch(options)
  return {
    url = "https://jfrog.com/cli-" .. options.version .. ".tar.gz",
    sha256 = "..."
  }
end

function install(ctx)
  ctx.extract_all()
end

function deploy(ctx)
  ctx.symlink(ctx.install_dir .. "/jf", ctx.project_root .. "/bin/jf")
end
```

### Phase 1 Execution Trace

```
1. Main thread spawns two TBB tasks:
   - Task A: fetch_recipes(openocd@v3, manifest.overrides)
   - Task B: fetch_recipes(jfrog@v2, manifest.overrides)

2. Task A:
   - seen.insert({openocd@v3, {version:"0.12.0"}}) → success
   - apply_overrides → url="https://example.com/recipes/openocd-v3.lua"
   - cache::ensure_recipe("vendor.openocd@v3", "aabbccdd...", url)
     → Downloads to ~/.cache/envy/recipes/vendor.openocd@v3.lua
   - execute_recipe → load openocd-v3.lua
   - Call make_depends({version: "0.12.0"})
     → Returns dependency: arm.gcc@v2 with needed_by="build"
   - Spawn Task C: fetch_recipes(gcc@v2, manifest.overrides)

3. Task B (parallel with Task A):
   - seen.insert({jfrog@v2, {version:"2.50.0"}}) → success
   - apply_overrides → Built-in recipe (no url)
   - Extract embedded recipe from binary to cache
   - execute_recipe → load jfrog-v2.lua
   - No make_depends → done

4. Task C (spawned by Task A):
   - seen.insert({gcc@v2, {version:"13.2.0"}}) → success
   - apply_overrides → Manifest override kicks in!
     → url="https://internal-mirror.company/gcc-v2.lua" (NOT public.com)
   - cache::ensure_recipe("arm.gcc@v2", "11223344...", url)
     → Downloads from internal mirror
   - execute_recipe → load gcc-v2.lua
   - No make_depends → done

5. task_group.wait() → all tasks complete

Recipe cache after phase 1:
  {
    {"vendor.openocd@v3", "aabbccdd..."} → ~/.cache/envy/recipes/vendor.openocd@v3.lua,
    {"envy.jfrog@v2", "<builtin>"} → ~/.cache/envy/recipes/envy.jfrog@v2.lua,
    {"arm.gcc@v2", "11223344..."} → ~/.cache/envy/recipes/arm.gcc@v2.lua
  }

Seen set:
  {
    {"vendor.openocd@v3", {version:"0.12.0"}},
    {"envy.jfrog@v2", {version:"2.50.0"}},
    {"arm.gcc@v2", {version:"13.2.0"}}
  }
```

### Phase 2 Execution Trace

```
1. resolve_recipe(openocd@v3, manifest.overrides):
   - visiting.insert({openocd@v3, opts}) → success
   - Load openocd-v3.lua from cache
   - introspect_verbs → ["fetch", "build", "install"]
   - Call make_depends({version: "0.12.0"})
   - Process dependency: arm.gcc@v2
     - needed_by="build" → Validate "build" in ["fetch","build","install"] ✓
     - Recurse: resolve_recipe(gcc@v2, manifest.overrides)

2. resolve_recipe(gcc@v2, manifest.overrides):
   - visiting.insert({gcc@v2, opts}) → success
   - Load gcc-v2.lua from cache (internal mirror version)
   - introspect_verbs → ["fetch", "install"]
   - No make_depends → no dependencies
   - visiting.erase({gcc@v2, opts})
   - visited.insert({gcc@v2, opts} → {identity:"arm.gcc@v2", verbs:["fetch","install"], deps:[]})
   - Return pointer to visited entry

3. Back in resolve_recipe(openocd@v3):
   - Store dependency: {target: gcc@v2, needed_by: "build"}
   - visiting.erase({openocd@v3, opts})
   - visited.insert({openocd@v3, opts} → {identity:"vendor.openocd@v3", verbs:["fetch","build","install"], deps:[gcc@v2]})

4. resolve_recipe(jfrog@v2, manifest.overrides):
   - visiting.insert({jfrog@v2, opts}) → success
   - Load jfrog-v2.lua from cache
   - introspect_verbs → ["fetch", "install", "deploy"]
   - No make_depends → no dependencies
   - visiting.erase({jfrog@v2, opts})
   - visited.insert({jfrog@v2, opts} → {identity:"envy.jfrog@v2", verbs:["fetch","install","deploy"], deps:[]})

5. errors vector is empty → success

Final resolved graph (visited map):
  {
    {"arm.gcc@v2", {version:"13.2.0"}} → {
      identity: "arm.gcc@v2",
      verbs: ["fetch", "install"],
      deps: []
    },
    {"vendor.openocd@v3", {version:"0.12.0"}} → {
      identity: "vendor.openocd@v3",
      verbs: ["fetch", "build", "install"],
      deps: [{target: gcc@v2, needed_by: "build"}]
    },
    {"envy.jfrog@v2", {version:"2.50.0"}} → {
      identity: "envy.jfrog@v2",
      verbs: ["fetch", "install", "deploy"],
      deps: []
    }
  }
```

### Resulting Verb DAG (Phase 3)

Phase 3 (not detailed here) constructs TBB flow graph from resolved recipes:

```
Nodes:
  N1: gcc:fetch
  N2: gcc:install
  N3: openocd:fetch
  N4: openocd:build
  N5: openocd:install
  N6: jfrog:fetch
  N7: jfrog:install
  N8: jfrog:deploy

Edges (dependencies):
  N1 → N2  (gcc internal: fetch before install)
  N3 → N4  (openocd internal: fetch before build)
  N4 → N5  (openocd internal: build before install)
  N6 → N7  (jfrog internal: fetch before install)
  N7 → N8  (jfrog internal: install before deploy)
  N2 → N4  (openocd depends on gcc, needed_by="build")

Execution order (one valid topological sort):
  Parallel wave 1: N1 (gcc:fetch), N3 (openocd:fetch), N6 (jfrog:fetch)
  Wave 2: N2 (gcc:install), N7 (jfrog:install)
  Wave 3: N4 (openocd:build), N8 (jfrog:deploy)
  Wave 4: N5 (openocd:install)
```

**Parallelism achieved**: All fetches run concurrently. JFrog install and deploy proceed independently while OpenOCD waits for GCC. Maximum throughput given dependency constraints.

## Error Handling

### Error Types

```cpp
enum class error_type {
  cycle,                 // Dependency cycle detected
  invalid_needed_by,     // needed_by references unimplemented verb
  missing_recipe,        // Recipe not in cache after phase 1
  conflicting_source,    // Same identity+sha256 from different URLs
  lua_error,             // Recipe script execution failed
  missing_identity,      // Recipe missing identity declaration
  security_violation     // Non-local recipe depends on local recipe
};

struct resolution_error {
  error_type type;
  std::string message;
  std::string recipe;        // Recipe where error occurred
  std::string dependency;    // Dependent recipe (if applicable)
  std::vector<std::string> path;  // Cycle path (if applicable)
};
```

### Example Error: Cycle Detection

```lua
-- Recipe A depends on B
function make_depends(options)
  return {{ recipe = "vendor.b@v1" }}
end

-- Recipe B depends on C
function make_depends(options)
  return {{ recipe = "vendor.c@v1" }}
end

-- Recipe C depends on A (cycle!)
function make_depends(options)
  return {{ recipe = "vendor.a@v1" }}
end
```

**Phase 2 trace**:
```
resolve_recipe(a@v1):
  visiting = {a@v1}
  Recurse: resolve_recipe(b@v1)
    visiting = {a@v1, b@v1}
    Recurse: resolve_recipe(c@v1)
      visiting = {a@v1, b@v1, c@v1}
      Recurse: resolve_recipe(a@v1)
        a@v1 in visiting → CYCLE DETECTED
        errors.push_back({
          type: cycle,
          message: "Dependency cycle detected at vendor.a@v1",
          path: ["vendor.a@v1", "vendor.b@v1", "vendor.c@v1", "vendor.a@v1"]
        })
        return nullptr
```

### Example Error: Invalid needed_by

```lua
-- Recipe only implements fetch and install
function fetch(options) ... end
function install(ctx) ... end

-- But declares dependency needed_by="build"
function make_depends(options)
  return {
    {
      recipe = "arm.gcc@v2",
      needed_by = "build"  -- ERROR: no build verb!
    }
  }
end
```

**Phase 2 detection**:
```cpp
verbs = introspect_verbs(recipe_obj);  // ["fetch", "install"]

for dep in raw_deps:
  if dep.needed_by == "build":
    if "build" not in verbs:
      errors.push_back({
        type: invalid_needed_by,
        message: "Recipe vendor.foo@v1 declares needed_by='build' for dependency arm.gcc@v2 but has no build verb",
        recipe: "vendor.foo@v1",
        dependency: "arm.gcc@v2"
      })
```

## Security Enforcement

### Local Recipe Isolation

Non-local recipes (remote, built-in) cannot depend on `local.*` recipes. Prevents remote code from assuming project-specific recipe availability.

```cpp
// During phase 2 resolution
for (const auto& dep_pkg : raw_deps) {
  bool parent_is_local = key.identity.starts_with("local.");
  bool child_is_local = dep_pkg.identity.starts_with("local.");

  if (!parent_is_local && child_is_local) {
    errors.push_back({
      .type = error_type::security_violation,
      .message = fmt::format(
        "Non-local recipe {} cannot depend on local recipe {}",
        key.identity, dep_pkg.identity),
      .recipe = key.identity,
      .dependency = dep_pkg.identity
    });
  }
}
```

**Allowed**: `local.wrapper@v1` depends on `arm.gcc@v2` (local can use remote)
**Forbidden**: `arm.gcc@v2` depends on `local.helper@v1` (remote cannot use local)

## Implementation Checklist

- [ ] Define `recipe_key`, `resolved_key`, `resolved_recipe` structs
- [ ] Implement `apply_overrides()` with manifest precedence
- [ ] Implement `merge_overrides()` for parent+recipe combination
- [ ] Implement phase 1: `fetch_recipes()` with TBB task spawning
- [ ] Implement `cache::ensure_recipe()` with lock handling
- [ ] Implement `execute_recipe()` with Lua script loading
- [ ] Implement `introspect_verbs()` for verb detection
- [ ] Implement phase 2: `resolve_recipe()` with DFS + memoization
- [ ] Implement cycle detection via `visiting` set
- [ ] Implement `needed_by` validation against verb list
- [ ] Implement local recipe security check
- [ ] Implement error accumulation and reporting
- [ ] Add unit tests for override precedence
- [ ] Add unit tests for cycle detection
- [ ] Add unit tests for `needed_by` validation
- [ ] Add functional tests with real Lua recipes
- [ ] Add functional tests for concurrent recipe fetching
- [ ] Document error messages with remediation steps

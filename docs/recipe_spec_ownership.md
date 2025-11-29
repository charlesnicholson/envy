# Recipe Spec Ownership Refactor Plan

Goal: centralize ownership of `recipe_spec` instances in a stable, pointer-safe pool (deque), eliminate ad-hoc cloning, and keep callers using raw non-owning pointers with clear lifetimes.

## Proposed Design

- Introduce a `recipe_spec_pool` that owns a `std::deque<recipe_spec>`, guarded by a mutex.
- `recipe_spec` remains uncopyable/unmovable; add a constructor that takes full parsed state so specs are emplaced directly into the deque under lock.
- All creation sites (manifest parse, recipe deps, fetch deps, weak fallbacks, nested fetch deps) obtain specs via the pool and receive raw non-owning pointers.
- The pool is scoped (owned by `engine` or test harness) and set via a static setter on `recipe_spec` to avoid global leakage across tests/runs.

## Tasks

- Add `recipe_spec_pool` type (deque + mutex) and `recipe_spec::set_pool(pool*)` / `recipe_spec::allocate(...)` API that emplaces and returns `recipe_spec*`.
- Make `recipe_spec` unmovable (keep non-copyable) and add a constructor that accepts all fields needed for emplacement.
- Update manifest parsing to allocate specs via the pool instead of constructing in-place vectors.
- Update recipe dependency parsing/collection to allocate via the pool (including nested `source.dependencies`).
- Update weak fallback handling to store pool-allocated specs instead of cloning/moving from const parents.
- Ensure custom fetch parsing (in recipe threads) uses the pool; guard pool emplace with the mutex.
- Provide a scoped pool for tests (setup/teardown helpers) to avoid cross-test leakage.
- Remove the temporary clone helper and any deep-copy workarounds once all call sites use the pool.
- Validate threading paths: weak resolution, fetch deps, and custom fetch parsing all interact safely with the pooled specs.
- Run full build + unit + functional suites after migration.

## Unit/Functional Test Additions

- Pool API: allocate returns stable pointers; multiple allocations don’t invalidate earlier pointers.
- Pool mutex safety: concurrent allocations from multiple threads succeed and produce distinct specs.
- Manifest parsing uses pool: specs created via manifest parsing remain alive and are unique per entry.
- Recipe dependency parsing uses pool: deps and nested `source.dependencies` produce pool-backed specs (strong and weak).
- Weak fallback handling: fallbacks are pool-backed; weak resolution uses those without cloning.
- Parent pointer integrity: parent fields are set correctly on pool-allocated specs across contexts.
- Custom fetch parsing: specs parsed during fetch (in recipe threads) allocate via pool without races.
- Pool scoping/reset: test harness can set/reset pools; no cross-test leakage; dangling pointers are avoided when pool is destroyed after engine/test scope.

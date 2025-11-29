# Recipe Spec Ownership Refactor Plan

Goal: centralize ownership of `recipe_spec` instances in a stable, pointer-safe pool (deque), eliminate ad-hoc cloning, and keep callers using raw non-owning pointers with clear lifetimes.

## Proposed Design

- Introduce a `recipe_spec_pool` that owns a `std::deque<recipe_spec>`, guarded by a mutex.
- `recipe_spec` remains uncopyable/unmovable; add a constructor that takes full parsed state so specs are emplaced directly into the deque under lock.
- All creation sites (manifest parse, recipe deps, fetch deps, weak fallbacks, nested fetch deps) obtain specs via the pool and receive raw non-owning pointers.
- The pool is scoped (owned by `engine` or test harness) and set via a static setter on `recipe_spec` to avoid global leakage across tests/runs.

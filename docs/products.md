# Products Feature Implementation

## Overview
Recipes can declare products (name→value map) and depend on products instead of explicit recipe identities. Products enable recipes to advertise entry points—for cached recipes, values are relative paths concatenated with asset_path; for user-managed recipes, values are raw strings (often shell commands). The `envy product <name>` command queries product values from the resolved graph.

## Implementation Tasks

### Data Structures

- [x] Add `std::unordered_map<std::string, std::string> products` to `recipe` struct in `src/recipe.h`
- [x] Add `std::optional<std::string> product` field to `recipe_spec` struct in `src/recipe_spec.h`
- [x] Add `bool is_product` flag to `recipe::weak_reference` struct in `src/recipe.h`
- [x] Add `std::string constraint_identity` to `recipe::weak_reference` struct in `src/recipe.h`
- [x] Add `std::unordered_map<std::string, recipe *> product_registry_` to `engine` class in `src/engine.h` (private)
- [x] Add `std::vector<std::string> resolved_weak_dependency_keys` to `recipe` struct for cache hash computation

### Parsing - Products Table

- [x] Parse `products` global from Lua in `src/phases/phase_recipe_fetch.cpp` (after `validate_phases()`, line 269)
- [x] Validate products is a table (or absent)
- [x] Validate all keys and values are strings
- [x] Validate keys and values are non-empty
- [x] Store parsed products in `recipe::products` map
- [x] Add appropriate error messages with recipe identity context
- [x] Support products as function taking options, returning table (dynamic product names)

### Parsing - Product Dependencies

- [x] Parse optional `product` field in `src/recipe_spec.cpp::parse()` (line 215, after parsing `recipe` field)
- [x] Validate `product` field is string type
- [x] Validate `product` field is non-empty if present
- [x] Store in `recipe_spec::product`
- [x] Update `recipe_spec::pool()->emplace()` call to include new `product` parameter (line 320)
- [x] Update `recipe_spec` constructor signature to accept product parameter
- [x] Allow product-only dependency tables (product with no recipe/source) to parse as ref-only product deps

### Dependency Wiring Semantics

- [x] Treat product deps like recipe deps for scheduling: strong product deps (have source) spawn immediately, run `recipe_fetch`, and wire `dependencies` with `needed_by`
- [x] Ref-only or weak product deps become `weak_reference` entries with `is_product=true`, `query=product`, `constraint_identity=dep_spec->identity` when present, `needed_by` propagated, and fallback pointer set
- [x] Ensure declared_dependencies records the resolved provider identity for ctx.asset validation

### Product Registry and Collision Detection

- [x] Add `void engine::update_product_registry()` method in `src/engine.cpp`
- [x] Iterate all recipes beyond recipe_fetch phase (check `current_phase > recipe_fetch`) after each `wait_for_resolution_phase()` iteration
- [x] Build collision map: `product_name → vector<recipe*>`
- [x] For products with multiple providers, collect error messages with all provider identities
- [x] For products with single provider, register in `product_registry_`
- [x] Throw aggregated error if any collisions detected (collision is always error, no priority rules)
- [x] Call `build_product_registry()` in `engine::resolve_graph()` after `wait_for_resolution_phase()`, before weak resolution; rebuild each iteration so late providers participate

### Product Dependency Resolution

- [x] Add product resolution branch to `engine::resolve_weak_references()` (~line 117)
- [x] Check `if (wr->is_product)` before existing identity resolution logic; all cycle checks mirror recipe deps
- [x] Lookup product in `product_registry_` (with mutex protection)
- [x] If found: validate `constraint_identity` matches provider if non-empty
- [x] If found: check for cycles using `has_dependency_path(provider, r)`
- [x] If found: wire dependency (add to `dependencies` map, `declared_dependencies` list)
- [x] If found: mark resolved, increment `result.resolved`
- [x] If not found and has fallback: instantiate fallback recipe (reuse weak fallback logic) and wire with `needed_by`
- [x] If not found and no fallback: collect in `result.missing_without_fallback`

### Transitive Product Validation

- [x] Add `void engine::validate_product_fallbacks()` method in `src/engine.cpp`
- [x] Iterate all recipes with product weak_references that have fallback and are resolved
- [x] Call `recipe_provides_product_transitively()` helper to validate
- [x] Collect validation errors for fallbacks that don't provide the required product
- [x] Throw aggregated error if any validation failures
- [x] Call `validate_product_fallbacks()` in `engine::resolve_graph()` after weak resolution converges
- [x] Add `bool engine::recipe_provides_product_transitively(recipe *r, std::string const &product_name)` helper method
- [x] Implement DFS with visited set checking `recipe::products` at each node
- [x] Recurse through `dependencies` map entries

### Cache Hash with Resolved Weak Dependencies

Resolved weak/ref-only dependencies must contribute to cache hash—different providers produce different packages. Pre-compute resolved keys in `recipe::resolved_weak_dependency_keys` after weak resolution converges (avoids data race). Hash format: `BLAKE3(identity{opts}|resolved1|resolved2|...)` with sorted keys. Strong dependencies don't contribute.

- [x] Populate `recipe::resolved_weak_dependency_keys` in `src/engine.cpp` after `validate_product_fallbacks()` (atomic with mutex)
- [x] Sort resolved keys for deterministic ordering
- [x] Modify `src/phases/phase_check.cpp` to include resolved weak dependency keys in hash computation
- [x] Read pre-computed `resolved_weak_dependency_keys` field (thread-safe, no race condition)
- [x] Skip programmatic recipes (only cache-managed recipes need hash)

### CLI Command Structure

- [x] Create `src/cmds/cmd_product.h` with command class following `cmd_asset` pattern
- [x] Define `cfg` struct inheriting from `cmd_cfg<cmd_product>` with `product_name`, `manifest_path`, `cache_root` fields
- [x] Create `src/cmds/cmd_product.cpp` implementation file
- [x] Implement constructor and `execute()` method
- [x] In `execute()`: load manifest, resolve cache root (follow `cmd_asset` pattern)
- [x] In `execute()`: create engine, call `run_full(m->packages)`
- [x] In `execute()`: call `engine::find_product_provider(product_name)`
- [x] In `execute()`: validate provider exists and products map contains key
- [x] In `execute()`: implement output logic—if programmatic or empty asset_path, return raw value; else concatenate asset_path with value
- [x] Add `recipe *engine::find_product_provider(std::string const &product_name)` public method to `src/engine.h`
- [x] Implement `find_product_provider()` in `src/engine.cpp` (simple registry lookup with mutex)
- [x] Share manifest/cache-root resolution helpers (no duplication)

### CLI Integration

- [x] Add `#include "cmds/cmd_product.h"` to `src/cli.cpp`
- [x] Add `cmd_product::cfg` to `cmd_cfg_t` variant in `src/cli.h`
- [x] Register `product` subcommand in `cli_parse()` in `src/cli.cpp`
- [x] Add required `product_name` positional argument
- [x] Add optional `--manifest` flag
- [x] Add optional `--cache-root` flag
- [x] Set callback to assign `cmd_cfg = product_cfg`

### Unit Tests - Recipe Spec Parsing

- [x] Add test for parsing `product` field in dependency table (`src/recipe_spec_tests.cpp`)
- [x] Add test for strong product dep: `{ product = "python3", recipe = "...", source = "..." }`
- [x] Add test for weak product dep: `{ product = "python3", weak = {...} }`
- [x] Add test for ref-only product dep: `{ product = "python3" }`
- [x] Add test rejecting non-string product field
- [x] Add test rejecting empty product field

### Unit Tests - CLI Parsing

- [x] Add test parsing `envy product python3` (`src/cli_tests.cpp`)
- [x] Add test for optional product_name argument (no args lists all products)
- [x] Add test with `--manifest` flag
- [x] Add test with `--cache-root` flag
- [x] Add test with both optional flags
- [x] Add test with `--json` flag

### Functional Test Fixtures

- [x] Create `test_data/recipes/product_provider.lua` with static products table
- [x] Create `test_data/recipes/product_consumer_strong.lua` with strong product dependency
- [x] Create `test_data/recipes/product_consumer_weak.lua` with weak product dependency
- [x] Create `test_data/recipes/product_ref_only_consumer.lua` with ref-only product dependency
- [x] Create `test_data/recipes/product_provider_b.lua` providing same product (for collision test)
- [ ] Create `test_data/recipes/product_transitive_root.lua` for transitive provision test
- [ ] Create `test_data/recipes/product_transitive_intermediate.lua` (middle of chain)
- [ ] Create `test_data/recipes/product_transitive_provider.lua` (actual provider at end of chain)
- [x] Create `test_data/recipes/product_provider_programmatic.lua` (user-managed, no asset_path)
- [x] Create `test_data/recipes/product_cycle_a.lua` and `product_cycle_b.lua` for cycle testing
- [x] Create `test_data/recipes/product_provider_function.lua` (products as function taking options)
- [x] Create `test_data/recipes/hash_consumer_weak.lua`, `hash_provider_a.lua`, `hash_provider_b.lua` for cache hash tests

### Functional Tests - Parsing

- [ ] Create `functional_tests/test_products_parsing.py`
- [ ] Add test for static products table parsing
- [ ] Add test for invalid products type (non-table)
- [ ] Add test for invalid product key type (non-string)
- [ ] Add test for invalid product value type (non-string)
- [ ] Add test for empty product key
- [ ] Add test for empty product value

### Functional Tests - Dependencies

- [x] Create `functional_tests/test_products.py`
- [x] Add test for strong product dependency resolution
- [x] Add test for weak product dependency with existing provider
- [x] Add test for weak product dependency using fallback
- [x] Add test for ref-only product dependency success
- [x] Add test for ref-only product dependency missing (error)
- [x] Add test for identity constraint validation (provider must match specified recipe)
- [ ] Add test for identity constraint mismatch error
- [x] Add test for programmatic products function with options
- [x] Add test for absolute path rejection in product values
- [x] Add test for path traversal rejection in product values
- [x] Add test that strong product deps wire directly, not via weak resolution

### Functional Tests - Collisions

- [x] Tests in `functional_tests/test_products.py`
- [x] Add test for collision between two providers (error with both identities)
- [ ] Add test for collision between three providers
- [x] Add test for no collision when different products provided

### Functional Tests - Transitive Provision

- [ ] Create `functional_tests/test_product_transitive.py`
- [ ] Add test for transitive provision chain (A→B→C, C provides)
- [ ] Add test for fallback doesn't transitively provide (error)
- [ ] Add test for fallback transitively provides via dependency

### Functional Tests - Cache Hash with Weak Dependencies

- [x] Create `functional_tests/test_weak_dep_hash.py`
- [x] Add test that different resolved providers produce different cache hashes
- [x] Add test that different fallback providers produce different cache hashes
- [x] Add test that resolved keys are sorted in hash computation
- [x] Add test for ref-only product dependencies in cache hash
- [x] Add test that strong dependencies don't contribute to cache hash

### Functional Tests - CLI Command

- [x] Tests in `functional_tests/test_products.py`
- [x] Add test for querying cached recipe product (returns concatenated path)
- [x] Add test for querying programmatic recipe product (returns raw value)
- [ ] Add test for querying non-existent product (error)
- [ ] Add test with `--manifest` flag
- [ ] Add test with `--cache-root` flag
- [x] Add test for product listing (no args, lists all products)
- [x] Add test for product listing with --json flag
- [x] Add test for programmatic products marked in listing
- [x] Add test for empty product list

### Functional Tests - Cycles

- [x] Add product cycle detection test in `functional_tests/test_products.py`
- [ ] Add test for direct cycle (A provides and depends on same product)
- [x] Add test for transitive cycle (A→B→A via product dependencies)

### Build Integration

- [x] Add `src/cmds/cmd_product.cpp` to CMakeLists.txt sources if not auto-discovered
- [x] Verify clean build with no warnings
- [x] Verify all unit tests pass (376 tests)
- [x] Verify all functional tests pass (654 tests)

### Documentation

- [ ] Add product dependencies section to `docs/recipe_resolution.md`
- [ ] Document products table syntax and semantics
- [ ] Document collision detection and error reporting
- [ ] Document transitive provision behavior
- [ ] Add examples of cached vs programmatic product usage

### Product Listing (envy product with no args)

The `envy product` command with no arguments lists all products from the resolved dependency graph. Output modes: aligned columns to stderr (human-readable) or JSON array to stdout (machine-readable via `--json`). The `engine::collect_all_products()` API iterates resolved recipes, extracts product name/value pairs with provider identity, programmatic flag, and asset_path. Results sorted alphabetically by product name. For programmatic recipes, asset_path is empty; for cached recipes, asset_path contains the cache directory.

- [x] Make `product_name` CLI argument optional in `src/cli.cpp`
- [x] Add `--json` flag to `cmd_product::cfg` for JSON output mode
- [x] Add `std::vector<product_info> engine::collect_all_products()` public method in `src/engine.h`
- [x] Implement `collect_all_products()` in `src/engine.cpp`: iterate recipes, extract products, include canonical identity, programmatic flag, asset_path
- [x] Sort results by product name alphabetically
- [x] Implement `list_all_products()` in `src/cmds/cmd_product.cpp` for human-readable aligned columns (stderr)
- [x] Format: `product_name  value  provider_canonical  (programmatic)` with column alignment, no dividers/headers
- [x] Implement JSON output mode: write array of objects to stdout with `product`, `value`, `provider`, `programmatic`, `asset_path` fields
- [x] Add functional test verifying all products appear in listing with correct provider identities
- [x] Add functional test verifying JSON output contains expected fields and values
- [x] Add functional test for empty product list (no products defined)
- [x] Update CLI tests in `src/cli_tests.cpp` to verify optional product_name argument and --json flag parsing

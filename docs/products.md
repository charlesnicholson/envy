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

### Parsing - Products Table

- [x] Parse `products` global from Lua in `src/phases/phase_recipe_fetch.cpp` (after `validate_phases()`, line 269)
- [x] Validate products is a table (or absent)
- [x] Validate all keys and values are strings
- [x] Validate keys and values are non-empty
- [x] Store parsed products in `recipe::products` map
- [x] Add appropriate error messages with recipe identity context

### Parsing - Product Dependencies

- [x] Parse optional `product` field in `src/recipe_spec.cpp::parse()` (line 215, after parsing `recipe` field)
- [x] Validate `product` field is string type
- [x] Validate `product` field is non-empty if present
- [x] Store in `recipe_spec::product`
- [x] Update `recipe_spec::pool()->emplace()` call to include new `product` parameter (line 320)
- [x] Update `recipe_spec` constructor signature to accept product parameter
- [ ] Allow product-only dependency tables (product with no recipe/source) to parse as ref-only product deps

### Dependency Wiring Semantics

- [x] Treat product deps like recipe deps for scheduling: strong product deps (have source) spawn immediately, run `recipe_fetch`, and wire `dependencies` with `needed_by`
- [x] Ref-only or weak product deps become `weak_reference` entries with `is_product=true`, `query=product`, `constraint_identity=dep_spec->identity` when present, `needed_by` propagated, and fallback pointer set
- [ ] Ensure declared_dependencies records the resolved provider identity for ctx.asset validation

### Product Registry and Collision Detection

- [x] Add `void engine::update_product_registry()` method in `src/engine.cpp`
- [x] Iterate all recipes that completed recipe_fetch (check `current_phase >= asset_check`) after each `wait_for_resolution_phase()` iteration
- [x] Build collision map: `product_name → vector<recipe*>`
- [x] For products with multiple providers, collect error messages with all provider identities
- [x] For products with single provider, register in `product_registry_`
- [x] Throw aggregated error if any collisions detected (collision is always error, no priority rules)
- [x] Call `build_product_registry()` in `engine::resolve_graph()` after `wait_for_resolution_phase()`, before weak resolution; rebuild each iteration so late providers participate

### Product Dependency Resolution

- [ ] Add product resolution branch to `engine::resolve_weak_references()` (~line 117)
- [ ] Check `if (wr->is_product)` before existing identity resolution logic; all cycle checks mirror recipe deps
- [ ] Lookup product in `product_registry_` (with mutex protection)
- [ ] If found: validate `constraint_identity` matches provider if non-empty
- [ ] If found: check for cycles using `has_dependency_path(provider, r)`
- [ ] If found: wire dependency (add to `dependencies` map, `declared_dependencies` list)
- [ ] If found: mark resolved, increment `result.resolved`
- [ ] If not found and has fallback: instantiate fallback recipe (reuse weak fallback logic) and wire with `needed_by`
- [ ] If not found and no fallback: collect in `result.missing_without_fallback`

### Transitive Product Validation

- [ ] Add `void engine::validate_product_fallbacks()` method in `src/engine.cpp`
- [ ] Iterate all recipes with product weak_references that have fallback and are resolved
- [ ] Call `recipe_provides_product_transitively()` helper to validate
- [ ] Collect validation errors for fallbacks that don't provide the required product
- [ ] Throw aggregated error if any validation failures
- [ ] Call `validate_product_fallbacks()` in `engine::resolve_graph()` after weak resolution converges
- [ ] Add `bool engine::recipe_provides_product_transitively(recipe *r, std::string const &product_name)` helper method
- [ ] Implement DFS with visited set checking `recipe::products` at each node
- [ ] Recurse through `dependencies` map entries

### CLI Command Structure

- [ ] Create `src/cmds/cmd_product.h` with command class following `cmd_asset` pattern
- [ ] Define `cfg` struct inheriting from `cmd_cfg<cmd_product>` with `product_name`, `manifest_path`, `cache_root` fields
- [ ] Create `src/cmds/cmd_product.cpp` implementation file
- [ ] Implement constructor and `execute()` method
- [ ] In `execute()`: load manifest, resolve cache root (follow `cmd_asset` pattern)
- [ ] In `execute()`: create engine, call `resolve_graph(m->packages)`
- [ ] In `execute()`: call `engine::find_product_provider(product_name)`
- [ ] In `execute()`: validate provider exists and products map contains key
- [ ] In `execute()`: implement output logic—if programmatic or empty asset_path, return raw value; else concatenate asset_path with value
- [ ] Add `recipe *engine::find_product_provider(std::string const &product_name)` public method to `src/engine.h`
- [ ] Implement `find_product_provider()` in `src/engine.cpp` (simple registry lookup with mutex)

### CLI Integration

- [ ] Add `#include "cmds/cmd_product.h"` to `src/cli.cpp`
- [ ] Add `cmd_product::cfg` to `cmd_cfg_t` variant in `src/cli.h`
- [ ] Register `product` subcommand in `cli_parse()` in `src/cli.cpp`
- [ ] Add required `product_name` positional argument
- [ ] Add optional `--manifest` flag
- [ ] Add optional `--cache-root` flag
- [ ] Set callback to assign `cmd_cfg = product_cfg`

### Unit Tests - Recipe Spec Parsing

- [ ] Add test for parsing `product` field in dependency table (`src/recipe_spec_tests.cpp`)
- [ ] Add test for strong product dep: `{ product = "python3", recipe = "...", source = "..." }`
- [ ] Add test for weak product dep: `{ product = "python3", weak = {...} }`
- [ ] Add test for ref-only product dep: `{ product = "python3" }`
- [ ] Add test rejecting non-string product field
- [ ] Add test rejecting empty product field

### Unit Tests - CLI Parsing

- [ ] Add test parsing `envy product python3` (`src/cli_tests.cpp`)
- [ ] Add test for missing product_name (required argument)
- [ ] Add test with `--manifest` flag
- [ ] Add test with `--cache-root` flag
- [ ] Add test with both optional flags

### Functional Test Fixtures

- [ ] Create `test_data/recipes/product_provider.lua` with static products table
- [ ] Create `test_data/recipes/product_dep_strong.lua` with strong product dependency
- [ ] Create `test_data/recipes/product_dep_weak.lua` with weak product dependency
- [ ] Create `test_data/recipes/product_dep_refonly.lua` with ref-only product dependency
- [ ] Create `test_data/recipes/product_collision_a.lua` providing a product
- [ ] Create `test_data/recipes/product_collision_b.lua` providing same product (for collision test)
- [ ] Create `test_data/recipes/product_transitive_root.lua` for transitive provision test
- [ ] Create `test_data/recipes/product_transitive_intermediate.lua` (middle of chain)
- [ ] Create `test_data/recipes/product_transitive_provider.lua` (actual provider at end of chain)
- [ ] Create `test_data/recipes/product_programmatic.lua` (user-managed, no asset_path)

### Functional Tests - Parsing

- [ ] Create `functional_tests/test_products_parsing.py`
- [ ] Add test for static products table parsing
- [ ] Add test for invalid products type (non-table)
- [ ] Add test for invalid product key type (non-string)
- [ ] Add test for invalid product value type (non-string)
- [ ] Add test for empty product key
- [ ] Add test for empty product value

### Functional Tests - Dependencies

- [ ] Create `functional_tests/test_product_dependencies.py`
- [ ] Add test for strong product dependency resolution
- [ ] Add test for weak product dependency with existing provider
- [ ] Add test for weak product dependency using fallback
- [ ] Add test for ref-only product dependency success
- [ ] Add test for ref-only product dependency missing (error)
- [ ] Add test for identity constraint validation (provider must match specified recipe)
- [ ] Add test for identity constraint mismatch error

### Functional Tests - Collisions

- [ ] Create `functional_tests/test_product_collisions.py`
- [ ] Add test for collision between two providers (error with both identities)
- [ ] Add test for collision between three providers
- [ ] Add test for no collision when different products provided

### Functional Tests - Transitive Provision

- [ ] Create `functional_tests/test_product_transitive.py`
- [ ] Add test for transitive provision chain (A→B→C, C provides)
- [ ] Add test for fallback doesn't transitively provide (error)
- [ ] Add test for fallback transitively provides via dependency

### Functional Tests - CLI Command

- [ ] Create `functional_tests/test_product_command.py`
- [ ] Add test for querying cached recipe product (returns concatenated path)
- [ ] Add test for querying programmatic recipe product (returns raw value)
- [ ] Add test for querying non-existent product (error)
- [ ] Add test with `--manifest` flag
- [ ] Add test with `--cache-root` flag

### Functional Tests - Cycles

- [ ] Add product cycle detection test to existing or new test file
- [ ] Add test for direct cycle (A provides and depends on same product)
- [ ] Add test for transitive cycle (A→B→A via product dependencies)

### Build Integration

- [ ] Add `src/cmds/cmd_product.cpp` to CMakeLists.txt sources if not auto-discovered
- [ ] Verify clean build with no warnings
- [ ] Verify all unit tests pass (374 tests + new tests)
- [ ] Verify all functional tests pass (610 tests + new tests)

### Documentation

- [ ] Add product dependencies section to `docs/recipe_resolution.md`
- [ ] Document products table syntax and semantics
- [ ] Document collision detection and error reporting
- [ ] Document transitive provision behavior
- [ ] Add examples of cached vs programmatic product usage

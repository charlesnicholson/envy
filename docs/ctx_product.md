# ctx.product Plan

## Summary
Expose `ctx.product(name)` to specs for product lookups with the same safety gates as `ctx.asset`: dependency validation, phase gating, and clear diagnostics. Share provider lookup logic with `cmd_product` so there is exactly one source of truth for product retrieval and readiness checks.

## Implementation Strategy
- Add a shared helper that resolves products (provider, value, asset_path handling, user-managed passthrough) and enforces phase/decl dependency rules; call it from both `cmd_product` and `ctx.product`.
- Phase awareness: do not cache phase in Lua ctx. Sample `current_phase` atomically from the recipe’s execution context (no locks, no cached copies).
- For `ctx.product`, validate declared product dependency, check `needed_by` ≤ current phase, then return `asset_path/value` or raw value for user-managed providers. Never block.
- For `ctx.asset`, reuse the same phase/readiness guard: strong-only reachability, first-hop `needed_by` gate, forbid user-managed providers. Return the asset path only when allowed.
- Add tracing around ctx access (asset/product) with phase, target, result, and reason; confirm JSON trace ordering with the functional tester.

## Checklist
- [x] Add docs here if anything shifts after review.
- [x] Provide phase access via execution context atomic (no cached phase in Lua ctx).
- [x] Record declared product deps (name, provider identity, needed_by) per recipe; include resolved weak product deps.
- [x] Build shared resolver: input = product name, caller recipe, engine, current phase; output = string or error. Use in `ctx.product` and `cmd_product`.
- [x] Asset enforcement: strong-only reachability helper with first-hop `needed_by` gate; forbid user-managed providers; integrate into `ctx.asset`.
- [x] Product enforcement: direct declared product dep lookup; `needed_by` gate; cache-managed returns joined path; user-managed returns raw value.
- [x] Tracing: add trace events for asset/product access attempts with phase, target, dependency edge used, allow/deny, reason.
- [x] Tests: unit tests for ctx.product/ctx.asset validation (phase too early, missing dep, user-managed provider, weak-only path, multiple strong paths choose earliest). 
- [ ] Functional tests: JSON trace ordering and an example spec using ctx.product to replace manual asset+path concat (e.g., local.gn).
- [x] Ensure no locks taken in hot path; use read-only snapshots of engine state.
- [x] Update CLI product command to reuse shared resolver; keep behavior identical.
- [x] Wire manifest-declared product deps into product_dependency metadata so ctx.product works for top-level packages.
- [x] Adjust dependency validation fixtures: mark needed_by appropriately or adapt tests for user-managed deps vs ctx.asset rules.
- [ ] Add dedicated ctx.product functional coverage (manifest product dep, weak product dep resolution, user-managed provider raw value).

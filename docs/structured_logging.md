# Structured Logging Implementation Plan

## Motivation

Current trace logging uses text parsing for tests, which is fragile and requires test updates when log messages change. Structured logging provides machine-readable output resilient to textual changes while maintaining human-readable traces.

**Immediate need:** Test the default `needed_by` phase bug fix without depending on timing or brittle regex parsing of log messages.

## Goals

1. **Machine-readable trace output** - JSONL format for robust test assertions
2. **Backward compatible** - Existing `--trace` behavior unchanged by default
3. **Flexible routing** - Output to stderr, file, or both simultaneously
4. **Non-intrusive API** - Minimal changes to call sites, natural C++ API
5. **Zero overhead when disabled** - Inline check avoids work when trace not enabled
6. **Test-friendly** - Enable precise phase ordering and dependency blocking assertions

## Design Overview

### Key Design Decisions

1. **Structured-only trace:** `tui::trace()` accepts ONLY structured events (typed structs), never freeform strings. All ~120 existing printf-style trace calls must migrate to structured events or move to `tui::debug()`.

2. **Simplified CLI:** `--trace` or `--trace=stderr` for human-readable stderr output, `--trace=file:path` for JSONL file output. Stderr trace is always human-readable, file trace is always JSONL. Multiple flags supported.

3. **Variant-based unified queue:** Log queue holds `std::variant<log_event, trace_event>` where `log_event` stores timestamp, severity, and formatted message for future processing flexibility.

4. **Zero-overhead when disabled:** `extern bool g_trace_enabled` enables early-exit inline check. Convenience macros use `ENVY_UNLIKELY` for branch prediction. No struct construction when tracing disabled.

5. **Fatal errors on write failure:** JSON file write failures exit immediately (no best-effort). Tests rely on complete trace, partial traces misleading.

6. **tui::init() accepts trace config:** Updated signature takes `std::vector<trace_output_spec>` with trace output destinations, opens JSON file at init (fatal on failure).

7. **Complete migration required:** Old printf-style `tui::trace()` removed entirely. Every call site must convert - no partial migration or mixed styles.

### CLI Interface

`--trace` flag accepts output specifiers. Multiple outputs supported via repeated flags.

**Syntax:**
```bash
# Human-readable to stderr (default/backward compatible)
envy --trace sync

# Human-readable to stderr (explicit)
envy --trace=stderr sync

# Structured JSONL to file
envy --trace=file:trace.jsonl sync

# Both simultaneously (multiple flags)
envy --trace=stderr --trace=file:trace.jsonl sync
```

**Flag parsing:**
```
--trace              → human-readable to stderr (default, backward compatible)
--trace=stderr       → human-readable to stderr (explicit)
--trace=file:<path>  → structured JSONL to file
```

**Design rationale:** Stderr trace is always human-readable for interactive use. File trace is always JSONL for test consumption. This avoids CLI ambiguity and keeps the common case simple.

### API Evolution

**Current API:**
```cpp
// Printf-style variadic args
tui::trace("engine: %s waiting for dep %s", identity, dep_name);
```

**New API (structured events ONLY):**
```cpp
// Strongly-typed structured events (only way to trace)
tui::trace(trace_event::phase_blocked{
  .recipe = identity,
  .blocked_at_phase = next,  // recipe_phase enum
  .waiting_for = dep_name,
  .target_phase = recipe_phase::completion
});

tui::trace(trace_event::dependency_added{
  .parent = r->spec.identity,
  .dependency = dep_cfg.identity,
  .needed_by = needed_by_phase  // recipe_phase enum
});

// Convenience macros for brevity (optional, use at call sites)
ENVY_TRACE_PHASE_BLOCKED(recipe, blocked_at_phase, waiting_for, target_phase);
ENVY_TRACE_DEPENDENCY_ADDED(parent, dependency, needed_by);
```

**Migration strategy:**
- **All existing `tui::trace()` calls MUST be converted to structured events** - no exceptions
- Printf-style `tui::trace()` will be removed completely - use `tui::debug()` for ad-hoc logging
- Every trace call must map to a specific event type with typed fields
- Structured events provide compile-time type safety and consistent formatting
- JSON output contains **only** structured events (no freeform strings possible)
- Other log levels (debug/info/warn/error) remain unchanged, always go to stderr with freeform strings

**Why structured-only trace:**
- Trace is for **sequence observation and debugging** - requires structured data
- Ad-hoc logging belongs in debug/info levels, not trace
- JSON trace must be parseable without string interpretation
- Forces explicit event definition, improving debuggability
- Enables robust test assertions on execution flow

### Zero-Overhead Design

**API signature (inline template wrapper):**
```cpp
// src/tui.h
extern bool g_trace_enabled;  // Defined in tui.cpp

template<typename EventT>
inline void trace(EventT&& event) {
  if (!g_trace_enabled) return;  // Early exit, no struct construction
  trace_impl_structured(trace_event{std::forward<EventT>(event)});
}

void trace_impl_structured(trace_event event);

// Convenience macros with C++20 branch prediction attributes
#define ENVY_TRACE_PHASE_BLOCKED(recipe, blocked, waiting, target) \
  if (::envy::tui::g_trace_enabled) [[unlikely]] { \
    ::envy::tui::trace(::envy::trace_event::phase_blocked{ \
      .recipe = (recipe), \
      .blocked_at_phase = (blocked), \
      .waiting_for = (waiting), \
      .target_phase = (target) \
    }); \
  }

#define ENVY_TRACE_DEPENDENCY_ADDED(parent, dep, needed) \
  if (::envy::tui::g_trace_enabled) [[unlikely]] { \
    ::envy::tui::trace(::envy::trace_event::dependency_added{ \
      .parent = (parent), \
      .dependency = (dep), \
      .needed_by = (needed) \
    }); \
  }

// (similar macros for all 22 event types...)
```

**Implementation:**
```cpp
// Updated tui::init() signature
namespace envy::tui {
  enum class trace_output_type { NONE, STDERR, FILE };
  struct trace_output_spec {
    trace_output_type type;
    std::optional<std::filesystem::path> file_path;  // Only for FILE type
  };
  void init(std::vector<trace_output_spec> trace_outputs = {});
}
```

`tui::init()` accepts trace output specs, sets `g_trace_enabled = !trace_outputs.empty()`, opens JSON file if needed (fatal error on failure), stores output specs internally. `trace_impl_structured()` routes to stderr formatter (human-readable) or JSON serializer (JSONL to file) based on output config.

**Benefits:** Early-exit on disabled trace (single bool check), no struct construction overhead, C++20 `[[unlikely]]` attribute guides branch prediction, compiler can optimize away trace calls in hot paths.

### Event Type Definitions

**Core principles:**
- Strongly-typed event structs (no freeform strings)
- Enum-based event types and phases
- Compile-time field validation
- Timestamps auto-added by tui layer

**Event variant and types:**

```cpp
// src/trace_event.h
namespace envy {

namespace trace_event {

// Event type definitions (structs with named fields)

struct phase_blocked {
  std::string recipe;
  recipe_phase blocked_at_phase;
  std::string waiting_for;
  recipe_phase target_phase;
};

struct phase_unblocked {
  std::string recipe;
  recipe_phase unblocked_at_phase;
  std::string dependency;
};

struct dependency_added {
  std::string parent;
  std::string dependency;
  recipe_phase needed_by;
};

struct phase_start {
  std::string recipe;
  recipe_phase phase;
};

struct phase_complete {
  std::string recipe;
  recipe_phase phase;
  int64_t duration_ms;
};

struct thread_start {
  std::string recipe;
  recipe_phase target_phase;
};

struct thread_complete {
  std::string recipe;
  recipe_phase final_phase;
};

struct recipe_registered {
  std::string recipe;
  std::string key;
  bool has_dependencies;
};

struct target_extended {
  std::string recipe;
  recipe_phase old_target;
  recipe_phase new_target;
};

// Lua context events (emitted from src/lua_ctx/*.cpp)

struct lua_ctx_run_start {
  std::string recipe;
  std::string command;  // Sanitized command (no secrets)
  std::string cwd;
};

struct lua_ctx_run_complete {
  std::string recipe;
  int exit_code;
  int64_t duration_ms;
};

struct lua_ctx_fetch_start {
  std::string recipe;
  std::string url;  // Sanitized URL (no credentials)
  std::string destination;
};

struct lua_ctx_fetch_complete {
  std::string recipe;
  std::string url;
  int64_t bytes_downloaded;
  int64_t duration_ms;
};

struct lua_ctx_extract_start {
  std::string recipe;
  std::string archive_path;
  std::string destination;
};

struct lua_ctx_extract_complete {
  std::string recipe;
  int64_t files_extracted;
  int64_t duration_ms;
};

// Cache and lock events (visibility into contention and hit rates)

struct cache_hit {
  std::string recipe;
  std::string cache_key;
  std::string asset_path;
};

struct cache_miss {
  std::string recipe;
  std::string cache_key;
};

struct lock_acquired {
  std::string recipe;
  std::string lock_path;
  int64_t wait_duration_ms;  // Time spent waiting for lock
};

struct lock_released {
  std::string recipe;
  std::string lock_path;
  int64_t hold_duration_ms;  // Time lock was held
};

// File-level fetch events (granular download tracking)

struct fetch_file_start {
  std::string recipe;
  std::string url;
  std::string destination;
};

struct fetch_file_complete {
  std::string recipe;
  std::string url;
  int64_t bytes_downloaded;
  int64_t duration_ms;
  bool from_cache;
};

}  // namespace trace_event

// Variant holding any trace event type
using trace_event = std::variant<
  trace_event::phase_blocked,
  trace_event::phase_unblocked,
  trace_event::dependency_added,
  trace_event::phase_start,
  trace_event::phase_complete,
  trace_event::thread_start,
  trace_event::thread_complete,
  trace_event::recipe_registered,
  trace_event::target_extended,
  trace_event::lua_ctx_run_start,
  trace_event::lua_ctx_run_complete,
  trace_event::lua_ctx_fetch_start,
  trace_event::lua_ctx_fetch_complete,
  trace_event::lua_ctx_extract_start,
  trace_event::lua_ctx_extract_complete,
  trace_event::cache_hit,
  trace_event::cache_miss,
  trace_event::lock_acquired,
  trace_event::lock_released,
  trace_event::fetch_file_start,
  trace_event::fetch_file_complete
>;

}  // namespace envy
```

**JSON output format:**

Events serialize to JSONL with enums converted to strings/numbers for tooling compatibility:

```json
{"ts":"2025-11-23T08:41:27.689Z","event":"phase_blocked","recipe":"local.ninja@r0","blocked_at_phase":"asset_check","blocked_at_phase_num":1,"waiting_for":"local.python@r0","target_phase":"completion","target_phase_num":7}
{"ts":"2025-11-23T08:41:36.228Z","event":"phase_unblocked","recipe":"local.ninja@r0","unblocked_at_phase":"asset_check","unblocked_at_phase_num":1,"dependency":"local.python@r0"}
{"ts":"2025-11-23T08:41:27.689Z","event":"dependency_added","parent":"local.ninja@r0","dependency":"local.python@r0","needed_by":"asset_check","needed_by_num":1}
{"ts":"2025-11-23T08:41:27.688Z","event":"phase_start","recipe":"local.ninja@r0","phase":"asset_fetch","phase_num":2}
{"ts":"2025-11-23T08:41:27.691Z","event":"phase_complete","recipe":"local.ninja@r0","phase":"asset_fetch","phase_num":2,"duration_ms":3}
{"ts":"2025-11-23T08:41:27.690Z","event":"cache_miss","recipe":"local.ninja@r0","cache_key":"ninja-v1.11.1"}
{"ts":"2025-11-23T08:41:27.692Z","event":"lock_acquired","recipe":"local.ninja@r0","lock_path":"/cache/ninja-v1.11.1/.lock","wait_duration_ms":0}
{"ts":"2025-11-23T08:41:27.693Z","event":"fetch_file_start","recipe":"local.ninja@r0","url":"https://example.com/ninja.tar.gz","destination":"/cache/ninja-v1.11.1/fetch/ninja.tar.gz"}
{"ts":"2025-11-23T08:41:28.234Z","event":"fetch_file_complete","recipe":"local.ninja@r0","url":"https://example.com/ninja.tar.gz","bytes_downloaded":1234567,"duration_ms":541,"from_cache":false}
{"ts":"2025-11-23T08:41:30.145Z","event":"lock_released","recipe":"local.ninja@r0","lock_path":"/cache/ninja-v1.11.1/.lock","hold_duration_ms":2453}
```

## Implementation Plan

### Phase 0: Trace Call Migration

**Goal:** Convert ALL existing `tui::trace()` calls to structured events or migrate to `tui::debug()`. No printf-style trace calls may remain.

**Files to audit:**
```bash
# Find all tui::trace calls
grep -rn "tui::trace" src/ --include="*.cpp" --include="*.h"
# Result: ~120 trace calls across engine, phases, cache, manifest, lua_ctx
```

**Decision criteria for each call:**
- **Keep as trace (convert to structured):** Sequence-relevant events (phase transitions, blocking, dependencies, thread lifecycle, cache operations, Lua context operations)
- **Move to debug:** Ad-hoc logging, temporary debugging output, verbose details not related to execution flow

**Example migrations:**

```cpp
// BEFORE (printf-style)
tui::trace("engine: %s waiting for dep %s to reach completion for phase %d",
           recipe, dep, phase_num);

// AFTER (structured event)
tui::trace(trace_event::phase_blocked{
  .recipe = recipe,
  .blocked_at_phase = next,
  .waiting_for = dep,
  .target_phase = recipe_phase::completion
});

**Migration workflow:**
1. Audit all trace calls, categorize as structural vs ad-hoc
2. For structural events, identify or create appropriate event type
3. Convert call sites to use structured event via macro
4. Move ad-hoc calls to `tui::debug()`
5. Remove old printf-style `tui::trace()` function signature entirely
6. Build and test

**Estimated effort:** 4-6 hours (audit ~120 trace calls, convert ~80-90 to structured, move ~30-40 to debug, define ~15-20 event types)

**Completion criteria:**
- Zero `tui::trace()` calls with printf-style args remain
- All trace events use structured event types
- Old printf-style `trace()` function removed from tui.h/tui.cpp
- Build succeeds with no warnings

---

### Phase 1: CLI and TUI Infrastructure

**Files modified:** `src/cli.cpp`, `src/cli.h`, `src/tui.h`, `src/tui.cpp`

**CLI changes:** Parse `--trace` flag with transform lambda: empty/"stderr" → human-readable stderr output, "file:<path>" → JSONL file output. Support multiple `--trace` flags. Default to stderr if flag present without arg (backward compat). Pass parsed specs to `tui::init()`.

**TUI architecture changes:**

**Define entry types:**
```cpp
// src/tui.cpp (internal to implementation)
struct log_event {
  std::chrono::system_clock::time_point timestamp;
  envy::tui::level severity;
  std::string message;  // Already formatted
};

using log_entry = std::variant<log_event, trace_event>;
```

**Queue and processing:**
- Existing log queue becomes `std::queue<log_entry>` (was `std::queue<std::string>`)
- All logging functions (debug/info/warn/error) enqueue `log_event` with timestamp, severity, formatted message
- `trace_impl_structured()` enqueues `trace_event` directly (no formatting yet)
- `tui::init()` accepts trace output specs, stores them, sets `g_trace_enabled = !trace_specs.empty()`, opens JSON file if needed (fatal on failure)
- Log processing thread uses `std::visit` or `std::holds_alternative` to dispatch based on variant type:
  - If `log_event`: write to stderr (respects level threshold, adds timestamp prefix if structured logging was requested)
  - If `trace_event`: route to trace handlers based on config:
    - Stderr trace: format human-readable message and write to stderr
    - File trace: serialize to JSONL and write to file (fatal on failure)

**Key changes:**
- `log_event` holds timestamp and severity for potential future processing (different severity levels, log filtering)
- Unified queue for all TUI output - single worker thread processes both types
- No separate trace thread or trace queue needed
- Backward compatible: existing logs go to stderr as before

**Tests:** CLI parsing all variants, multiple flags, invalid formats, `is_trace_enabled()` correctness.

---

### Phase 2: Structured Event API

**Files modified:** `src/trace_event.h` (new), `src/tui.cpp`

**Implementation:** `trace_impl_structured()` generates ISO8601 timestamp, routes event to configured outputs. For stderr, uses visitor to format human-readable message (e.g., "engine: X waiting for dep Y"). For JSON, uses visitor to serialize typed event: emit `{"ts":"...","event":"...","field1":"...",...}` with enums converted to strings+numbers. Use raw string literals for JSON. Handle escape sequences in JSON strings (`"`, `\`, `\n`, etc.). Fatal exit on write failure.

**Serialization approach:** Visitor pattern with `if constexpr` type checks. For each event type, emit corresponding JSON fields. Include both enum string name and numeric value for tooling convenience (e.g., `"phase":"asset_check","phase_num":1`).

**Tests:** JSON serialization all event types, ISO8601 formatting, JSON escaping, human-readable formatting, early-exit optimization, type safety.

---

### Phase 3: Emit All Events

**Goal:** Emit all structured events throughout the codebase for complete execution visibility.

**Files modified:** `src/engine.cpp`, `src/engine_phases/*.cpp`, `src/cache.cpp`, `src/lua_ctx/*.cpp`

**Event categories:**

**1. Dependency and blocking events (engine.cpp):**
```cpp
// Around line 249-256 - phase blocking
for (auto const &[dep_identity, dep_info] : r->dependencies) {
  if (next >= dep_info.needed_by) {
    ENVY_TRACE_PHASE_BLOCKED(r->spec.identity, next, dep_identity, recipe_phase::completion);
    ensure_recipe_at_phase(dep_info.recipe_ptr->key, recipe_phase::completion);
    ENVY_TRACE_PHASE_UNBLOCKED(r->spec.identity, next, dep_identity);
  }
}

// Around line 163 - target extension
ENVY_TRACE_TARGET_EXTENDED(r->spec.identity, current_target, target);
```

**2. Dependency registration (phase_recipe_fetch.cpp):**
```cpp
// Around line 230
recipe_phase const needed_by_phase{ dep_cfg.needed_by.has_value()
                                        ? static_cast<recipe_phase>(*dep_cfg.needed_by)
                                        : recipe_phase::asset_check };

r->dependencies[dep_cfg.identity] = { dep, needed_by_phase };
ENVY_TRACE_DEPENDENCY_ADDED(r->spec.identity, dep_cfg.identity, needed_by_phase);
```

**3. Thread lifecycle events (engine.cpp):**
```cpp
// Around line 67 - thread start
void engine::recipe_execution_ctx::start(recipe *r, engine *eng, std::vector<std::string> chain) {
  ancestor_chain = std::move(chain);
  ENVY_TRACE_THREAD_START(r->spec.identity, target_phase.load());
  worker = std::thread([r, eng] { eng->run_recipe_thread(r); });
}

// Around line 225 - thread exit (in run_recipe_thread after loop)
if (target == recipe_phase::completion) {
  ENVY_TRACE_THREAD_COMPLETE(r->spec.identity, current);
  break;
}
```

**4. Phase lifecycle events (all phase_*.cpp files):**
```cpp
// At start of each run_*_phase() function
void run_fetch_phase(recipe *r, engine &eng) {
  std::string const key{ r->spec.format_key() };
  ENVY_TRACE_PHASE_START(r->spec.identity, recipe_phase::asset_fetch);
  auto start_time = std::chrono::steady_clock::now();

  // ... existing phase logic ...

  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now() - start_time).count();
  ENVY_TRACE_PHASE_COMPLETE(r->spec.identity, recipe_phase::asset_fetch, duration);
}
```

Apply to all 8 phase functions: recipe_fetch, asset_check, asset_fetch, asset_stage, asset_build, asset_install, asset_deploy, completion.

**5. Cache events (cache.cpp):**
```cpp
// In ensure_entry() around line 58 - cache hit
if (is_complete(entry_dir)) {
  ENVY_TRACE_CACHE_HIT(recipe_identity, cache_key, asset_path.string());
  return cache_result{ std::nullopt, asset_dir() };
}

// Around line 77 - cache miss
ENVY_TRACE_CACHE_MISS(recipe_identity, cache_key);
```

**6. Lock events (cache.cpp scoped_entry_lock):**
```cpp
// Constructor around line 88
scoped_entry_lock::scoped_entry_lock(...) {
  auto lock_start = std::chrono::steady_clock::now();
  // ... acquire lock ...
  auto wait_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now() - lock_start).count();
  ENVY_TRACE_LOCK_ACQUIRED(recipe_identity, lock_path.string(), wait_duration);
  m->lock_acquired_time = std::chrono::steady_clock::now();
}

// Destructor around line 105
scoped_entry_lock::~scoped_entry_lock() {
  auto hold_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now() - m->lock_acquired_time).count();
  // ... cleanup logic ...
  ENVY_TRACE_LOCK_RELEASED(recipe_identity, lock_path.string(), hold_duration);
}
```

**7. Lua context events (lua_ctx/*.cpp):**

Already defined in event types: `lua_ctx_run_start`, `lua_ctx_run_complete`, `lua_ctx_fetch_start`, `lua_ctx_fetch_complete`, `lua_ctx_extract_start`, `lua_ctx_extract_complete`.

Emit at entry/exit of `lua_ctx_run()`, `lua_ctx_fetch()`, `lua_ctx_commit_fetch()` in respective files.

**8. File-level fetch events (phase_fetch.cpp):**
```cpp
// In download loop around line 430
for (size_t idx : to_download_indices) {
  auto const &remote_file = remote_files[idx];
  ENVY_TRACE_FETCH_FILE_START(r->spec.identity, remote_file.url, dest.string());

  auto start_time = std::chrono::steady_clock::now();
  // ... download logic ...
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now() - start_time).count();

  ENVY_TRACE_FETCH_FILE_COMPLETE(r->spec.identity, remote_file.url,
                                  file_size, duration, false);
}
```

**Tests:**
- Unit tests: Verify each macro expands correctly and emits events
- Functional test: Verify all event types present in trace output
- Functional test: Verify phase_blocked events for default_needed_by test
- Functional test: Verify dependency_added events contain correct needed_by
- Functional test: Verify cache hit/miss events for repeated builds
- Functional test: Verify lock contention events for parallel recipes

---

### Phase 4: Test Infrastructure

**New file: `functional_tests/trace_parser.py`**

**Helper functions:**
- `parse_trace_events(path)` - Parse JSONL file into list of event dicts
- `filter_events(events, event_type, **filters)` - Filter by type and field values
- `get_first_phase_block(events, recipe, dependency)` - Get phase number of first block
- `get_dependency_needed_by(events, parent, dependency)` - Get needed_by phase for dep
- `assert_phase_order(events, recipe, expected_phases)` - Assert phases in order
- `get_recipes_in_phase_at_time(events, phase, timestamp)` - Recipes in phase at time (parallelism check)

**Tests:** Parse valid JSONL, handle malformed lines, filter with criteria, phase ordering assertions.

---

### Phase 5: Default needed_by Test

**New file: `functional_tests/test_default_needed_by.py`**

**Test 1:** `test_default_needed_by_is_fetch_not_check` - Run `envy --trace=stderr --trace=json:trace.jsonl engine-test` on parent recipe depending on dep (no explicit needed_by). Parse trace, assert `dependency_added` event has `needed_by_num==2`. Assert first `phase_blocked` event (if any) also at phase 2.

**Test 2:** `test_explicit_needed_by_check_still_works` - Same setup but dep has explicit `needed_by="check"`. Assert `dependency_added` has `needed_by_num==1` and blocking occurs at phase 1.

**Test recipes:** Three minimal Lua files: `default_needed_by_parent.lua` (depends on dep, no needed_by), `default_needed_by_dep.lua` (simple), `explicit_check_parent.lua` (depends on simple@v1 with needed_by="check"). All use test.tar.gz from test_data.

---

### Phase 6: Bug Fix and Validation

**Apply fix:** Change `phase_recipe_fetch.cpp:227` default from `recipe_phase::asset_check` to `recipe_phase::asset_fetch`.

**Validation:** Run test (expect failure with "needed_by is 1, should be 2"), apply fix, rebuild, run test (expect pass).

---

## Alternative Designs Considered

### JSON Library Dependency

**Rejected:** Adding nlohmann/json or similar would simplify serialization but increases binary size and build complexity. Envy prioritizes lean binaries. Lightweight hand-rolled JSON sufficient for flat key-value events.

### Separate Binary for Testing

**Rejected:** Could build separate `envy_test` binary with structured logging always enabled. Rejected because real-world trace validation more valuable (same binary users run).

### Protobuf/MessagePack

**Rejected:** Binary formats more efficient but not human-inspectable. JSONL strikes balance: machine-readable, human-debuggable, widely supported tooling.

### Event Buffering/Async Write

**Deferred:** Could buffer events and flush periodically for performance. Not needed initially—trace only enabled for tests/debugging, not production. Inline early-exit ensures zero overhead when disabled. Revisit if trace overhead becomes measurable.

### Thread-Local Event Queues

**Deferred:** Could avoid mutex contention with thread-local queues merged at shutdown. Premature optimization—profile first if trace mutex shows contention.

### Non-inline trace() with macro wrapper

**Rejected:** Could use `#define tui_trace(...) if (tui::is_trace_enabled()) tui::trace_impl(__VA_ARGS__)` to avoid inline functions. Rejected because inline functions provide type safety, better debugging, and modern C++ compilers optimize inline checks effectively.

## Testing Strategy

### Unit Tests

- `src/tui_tests.cpp`:
  - CLI flag parsing (all variants, multiple flags, invalid formats)
  - JSON serialization (all lua_value types)
  - JSON string escaping (quotes, backslashes, control chars)
  - ISO8601 formatting
  - Human-readable fallback formatting
  - Early-exit behavior (`is_trace_enabled()` false → no work done)

### Functional Tests

- `functional_tests/test_trace_parser.py`:
  - Parse valid JSONL
  - Handle malformed lines
  - Filter events with criteria
  - Phase ordering assertions

- `functional_tests/test_default_needed_by.py`:
  - Default needed_by produces correct events
  - Blocking behavior matches needed_by phase
  - Explicit needed_by still works

- `functional_tests/test_structured_trace.py` (smoke test):
  - Verify JSON output well-formed
  - Verify stderr text output still works
  - Verify multiple outputs simultaneously

### Integration with Existing Tests

**Migration required** - All existing `tui::trace()` calls must be converted to structured events as part of this work. This ensures consistency and enables comprehensive test coverage from day one.

## Performance Considerations

### Overhead Analysis

**With inline early-exit optimization:**
- **Trace disabled** (normal operation): Single bool load + branch prediction with `[[unlikely]]` (effectively zero overhead)
- **Trace enabled** (testing/debugging):
  - Event struct construction and variant wrapping per event
  - JSON serialization (string formatting, escaping)
  - Mutex lock per event write (if JSON output enabled)

**Mitigation:**
- Inline check with C++20 `[[unlikely]]`: ~1-2 CPU cycles when disabled
- Branch predictor learns trace-disabled pattern quickly (guided by `[[unlikely]]` attribute)
- Trace only enabled for testing/debugging (negligible overhead in real usage)
- JSON output opt-in via `--trace=file:*` flag
- Small event structs (5-10 fields max)

**Measurement plan:**
- Benchmark sync with no trace vs `--trace=stderr` vs `--trace=stderr --trace=file:trace.jsonl`
- Target overhead with trace disabled: <0.1% (within noise)
- Target overhead with trace enabled: <5%
- If exceeded, implement event buffering

### Memory Usage

**Per-event overhead:**
- Event struct: ~100-200 bytes (varies by event type, includes strings)
- JSON serialization buffer: ~200-300 bytes
- Total: ~300-500 bytes per event (only allocated when trace enabled)

**Typical trace:**
- 100 recipes × 7 phases × ~5 events per phase = ~3,500 events
- Memory: ~1.5 MB (transient, freed after write)
- Acceptable for test/debug scenarios

### Disk I/O

**JSON file writes:**
- Flush after each event (durability for test validation)
- Typical event: 200-300 bytes
- 2,100 events: ~500 KB trace file
- Negligible I/O impact

## Documentation Updates

### User-Facing Documentation

Update `docs/commands.md`:
```markdown
### trace

Enable detailed execution tracing. Can output to stderr (human-readable) or JSONL files (machine-readable).

**Syntax:**
- `--trace` - Human-readable trace to stderr (default, backward compatible)
- `--trace=stderr` - Explicit human-readable to stderr
- `--trace=file:<path>` - Structured JSONL to file

**Multiple outputs:**
```bash
# Both human-readable and JSONL simultaneously
envy --trace=stderr --trace=file:trace.jsonl sync
```

**Note:** Stderr trace is always human-readable. File trace is always JSONL format.

**JSON format:**
Each line is a JSON object representing a trace event. Common fields:
- `ts` (string): ISO8601 timestamp
- `event` (string): Event type (e.g., "phase_blocked", "dependency_added")
- `recipe` (string): Recipe identity

See `docs/structured_logging.md` for complete event schema.
```

### Developer Documentation

Create/update:
- `docs/structured_logging.md` - This document (implementation plan → reference)
- `docs/testing.md` - Add section on structured trace testing
- `CLAUDE.md` - Update testing guidelines to prefer structured events over log parsing

## Timeline Estimate

**Phase 0 (Migration):** 4-6 hours
- Audit all ~120 trace calls, categorize: 0.5-1 hour
- Define ~15-20 event types in trace_event.h: 0.5-1 hour
- Convert ~80-90 calls to structured events: 2-3 hours
- Move ~30-40 non-sequence calls to debug: 0.5-1 hour
- Remove old printf-style trace() signature: 0.25 hour
- Testing: 0.5 hour

**Phase 1 (CLI/TUI infrastructure):** 4-6 hours
- CLI parsing: 1 hour
- TUI init changes: 1 hour
- Inline wrapper implementation: 1 hour
- File handling: 1 hour
- Unit tests: 1-2 hours

**Phase 2 (Structured API):** 5-7 hours
- Event type definitions (trace_event.h): 1 hour
- trace_impl functions: 1 hour
- JSON serialization (visitor): 2-3 hours
- Human-readable formatting (visitor): 1 hour
- Unit tests: 1-2 hours

**Phase 3 (Emit all events):** 8-12 hours
- Define all 22 ENVY_TRACE_XXX macros in tui.h: 1-2 hours
- Phase lifecycle events (8 phase functions × 2 events): 2-3 hours
- Thread lifecycle events: 0.5 hour
- Dependency and blocking events: 1 hour
- Cache hit/miss events: 1 hour
- Lock acquired/released events: 1-2 hours
- Lua context events (6 types): 1-2 hours
- File-level fetch events: 1 hour
- Testing: 1-2 hours

**Phase 4 (Test infrastructure):** 2-3 hours
- trace_parser.py: 1-2 hours
- Unit tests for parser: 1 hour

**Phase 5 (Default needed_by test):** 1-2 hours
- Test implementation: 0.5 hour
- Recipe files: 0.5 hour
- Validation: 0.5 hour

**Phase 6 (Bug fix validation):** 0.5 hour

**Total:** 24-38 hours (includes complete trace call migration and all event types)

**Deferred (future work):**
- Performance optimization (event buffering/async writes if trace overhead measured >5%)
- Chrome tracing format export
- OpenTelemetry integration
- CLI trace analysis tool

## Success Criteria

1. **Test passes after fix:**
   - `test_default_needed_by_is_fetch_not_check` fails before fix
   - Same test passes after changing asset_check → asset_fetch

2. **Backward compatibility:**
   - All existing functional tests pass unchanged
   - `--trace` without argument works as before

3. **No performance regression:**
   - Builds without `--trace` show <0.1% overhead (within measurement noise)
   - Builds with `--trace=stderr` show <1% overhead vs. without flag
   - Builds with `--trace=json:*` show <5% overhead vs. `--trace=stderr`

4. **Code quality:**
   - All new code covered by unit tests
   - Build passes with zero warnings
   - Follows existing code style (clang-format)
   - Inline wrappers maintain zero overhead when trace disabled

5. **Documentation complete:**
   - User-facing flag documentation in docs/commands.md
   - Event schema documented in this file
   - Testing guidelines updated

## Future Enhancements (Out of Scope)

### Additional Event Types (If Needed Later)

- **Build output capture:** Capture stdout/stderr from build phases in structured format
- **Performance metrics:** CPU time, memory usage, syscall counts per phase
- **Network metrics:** DNS lookup time, connection time, TLS handshake time for fetches

### Advanced Analysis Tools

- **CLI tool:** `envy trace-analyze trace.jsonl` for common queries (critical path, bottlenecks)
- **Visualization:** Generate timeline charts from trace (HTML/SVG output)
- **Diff tool:** Compare traces from two runs to identify regressions

### Integration with External Tools

- **Chrome tracing format:** Convert JSONL to chrome://tracing JSON for visual timeline
- **OpenTelemetry:** Export traces to OTLP for distributed tracing integration
- **Profiling hooks:** Correlate trace events with CPU/memory profiler data

## Design Decisions

### 1. Structured Events Only

**Decision:** `tui::trace()` accepts **only** structured events. Printf-style trace removed.

**Rationale:**
- Trace is for sequence observation and debugging - requires structured, machine-parseable data
- Ad-hoc logging belongs in `tui::debug()`, not trace
- JSON trace must be parseable without string interpretation
- Forces explicit event definition, improving debuggability and tooling

**Migration:** All existing `tui::trace()` calls converted to structured events or moved to `tui::debug()`.

### 2. Lua Context Events

**Decision:** Lua bindings (`src/lua_ctx/*.cpp`) emit structured events for all operations.

**Event types added:**
- `lua_ctx_run_start` / `lua_ctx_run_complete` - ctx.run() execution
- `lua_ctx_fetch_start` / `lua_ctx_fetch_complete` - ctx.fetch() operations
- `lua_ctx_extract_start` / `lua_ctx_extract_complete` - archive extraction

**Rationale:**
- Provides visibility into recipe Lua execution for debugging
- Captures timing data for performance analysis
- Enables tracing full recipe pipeline (C++ engine + Lua operations)

### 3. JSON Trace Scope

**Decision:** JSON trace contains **only** structured events. Other log levels (debug/info/warn/error) only go to stderr.

**Rationale:**
- Clean separation: trace = sequence observation, other levels = diagnostics
- JSON focused on machine parsing for test assertions
- Keeps JSON compact and focused
- stderr remains human-readable with all context

### 4. JSON Write Failures

**Decision:** JSON write failures are **fatal errors** in all scenarios, unconditionally.

**Rationale:**
- If trace is enabled, it's critical for debugging - failure is unacceptable
- Best-effort tracing leads to incomplete data and misleading results
- Tests depend on complete traces for correctness assertions
- Fail fast and loudly rather than silently producing bad data

**Implementation:** Exit with status 1 on file open failure or write failure.

### 5. Global State Encapsulation

**Decision:** `g_trace_enabled` exposed as `extern` in header, defined in tui.cpp.

**Rationale:**
- Necessary for inline `is_trace_enabled()` optimization
- Set once at init, read-only thereafter - no synchronization needed
- Standard pattern for performance-critical global flags
- Not the most encapsulated, but acceptable for this purpose

## Approval Checklist

Before implementation:
- [ ] Review event type definitions (all 22 event types - engine, cache, locks, Lua context, file-level fetch)
- [ ] Approve structured-events-only approach (no printf-style trace, ALL ~120 calls must migrate)
- [ ] Approve API design (inline template wrapper, ENVY_TRACE_XXX macros with C++20 `[[unlikely]]`)
- [ ] Approve simplified CLI syntax (--trace/--trace=stderr for human-readable, --trace=file:path for JSONL)
- [ ] Approve variant-based unified queue (`std::variant<log_event, trace_event>`)
- [ ] Approve log_event struct (holds timestamp, severity, formatted message)
- [ ] Approve tui::init() signature change (accepts `std::vector<trace_output_spec>`)
- [ ] Approve fatal error on JSON write failure (exit status 1, unconditional)
- [ ] Approve `extern bool g_trace_enabled` in header for inline optimization
- [ ] Review Phase 0 migration strategy (ALL ~120 trace calls migrate, ~80-90 to structured, ~30-40 to debug)
- [ ] Review Phase 3 event coverage (all 22 event types, no deferrals)
- [ ] Review timeline estimate (24-38 hours including complete migration and all events)
- [ ] Confirm this is acceptable detour from weak dependencies work

**Design decisions to confirm:**
1. **Structured events only** - All trace calls use typed events, printf-style removed entirely
2. **Simplified CLI** - Stderr always human-readable, file always JSONL (no ambiguity)
3. **Variant queue** - Unified `std::variant<log_event, trace_event>` queue, single worker thread
4. **log_event structure** - Holds timestamp+severity+message for future processing flexibility
5. **Complete migration** - All existing trace calls must convert (no partial migration)
6. **Comprehensive event coverage** - All 22 event types included (phase lifecycle, thread lifecycle, dependencies, blocking, cache, locks, Lua context, file-level fetch). No deferrals.
7. **Lua context events** - C++ bindings emit structured events for ctx operations
8. **JSON scope** - Only structured events in JSON, other logs stderr-only
9. **Fatal errors** - JSON write failures exit with status 1
10. **Global state** - `g_trace_enabled` exposed as extern for inline optimization
11. **tui::init() change** - Accepts trace config, opens JSON file at init (breaking API change)
12. **C++20 attributes** - Use `[[unlikely]]` directly in macros (cleaner than `__builtin_expect`, no platform.h changes needed)

After approval, proceed with Phase 0 (trace call migration).

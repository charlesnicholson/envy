# Immediate-Mode TUI: Progress Sections & Interactive Mode

## Concept

**Immediate-Mode TUI (IMTUI)**: Stateless rendering—callers provide complete frame data each update. TUI is pure function: `(frame, width, now, ansi) → string`. Worker thread caches frames, renders at 30fps. No animation state—spinners computed from timestamps, progress bars show current values.

**Benefits**: Deterministic rendering, full unit testability, no state sync between TUI/callers, centralized animation logic.

## Architecture

**Section state** (minimal): Vector of sections (allocation order = render order). Each: handle, active flag, cached frame. No animation state.

**Render cycle** (30fps):
1. ANSI: Clear previous progress region (move cursor up, clear to end). Fallback: no-op.
2. Flush log queue (logs print in cleared space)
3. Get terminal width (syscall)
4. Get current time
5. For each active section: `render_section_frame(cached_frame, width, ansi, now)`
6. ANSI: Update line count for next clear. Fallback: throttle (2s), print if changed.

**Critical ordering**: Clear BEFORE flush prevents logs from being erased. Logs print where old progress was, new progress renders below logs.

**Terminal detection**: Width via `ioctl(TIOCGWINSZ)` or `GetConsoleScreenBufferInfo` each frame. ANSI: TTY + `TERM != "dumb"` + Windows VT enabled.

**Interactive mode**: Global mutex serializes recipes needing terminal control (sudo, installers). Acquire locks, pauses rendering. Release unlocks, resumes.

**TUI Actions Architecture** (centralized progress tracking): Phases delegate TUI management to `tui_actions` helpers (`src/tui_actions.h`, `src/tui_actions.cpp`). Each helper owns section lifecycle, formats progress data, updates frames. Phases create tracker, pass to operations, tracker auto-manages. Benefits: single-responsibility, consistent formatting, testable in isolation.

**ctx.run() integration**: `make_ctx_run()` in `lua_ctx_run.cpp` auto-creates `run_progress` tracker when recipe has `tui_section`. Lua code calling `ctx.run()` gets TUI integration automatically—no phase-specific code. BUILD phase leverages this: string-returning BUILD functions route through `ctx.run()` (line 77), output captured to section. CHECK/INSTALL phases still use direct `shell_run()` calls with `tui::info()` callbacks—pending migration to `run_progress` pattern.

## API

### Section Frame Types (`src/tui.h`)

```cpp
namespace envy::tui {

struct progress_data {
  double percent;        // 0-100
  std::string status;    // "2.5MB/100MB downloading"
};

struct text_stream_data {
  std::vector<std::string> lines;                        // Full buffer
  std::size_t line_limit{0};                             // 0 = show all, N = show last N
  std::chrono::steady_clock::time_point start_time;      // For spinner animation
};

struct spinner_data {
  std::string text;
  std::chrono::steady_clock::time_point start_time;  // TUI computes frame
  std::chrono::milliseconds frame_duration{100};
};

struct static_text_data {
  std::string text;
};

struct section_frame {
  std::string label;  // Mutable: "pkg@v1", "pkg@v1 ✓"
  std::variant<progress_data, text_stream_data, spinner_data, static_text_data> content;
};

}
```

### Public API

```cpp
// Lifecycle (thread-safe)
section_handle section_create();
void section_set_content(section_handle h, section_frame const& frame);
void section_release(section_handle h);

// Interactive mode
void acquire_interactive_mode();
void release_interactive_mode();
class interactive_mode_guard { ... };  // RAII
```

### Test API (`ifdef ENVY_UNIT_TEST`)

```cpp
namespace envy::tui::test {
  extern int g_terminal_width;
  extern bool g_isatty;
  extern std::chrono::steady_clock::time_point g_now;

  std::string render_section_frame(section_frame const& frame);
}
```

## Rendering

### ANSI Mode

Clear previous (`\x1b[NF\x1b[0J`), render all sections, count lines for next clear.

**Progress bar**: `[label] status [========>     ] 42.5%`
**Text stream**: `[label] build output:\n   line1\n   line2`
**Spinner**: `[label] | text` (frames: `|/-\`, from elapsed time)
**Static**: `[label] text`

### Fallback Mode

Throttle 2s, print only if changed.

**Progress bar**: `[label] status: 42.5%`
**Text stream**: `[label] build output:\n   line1\n   line2`
**Spinner**: `[label] text....` (dots based on elapsed seconds)
**Static**: `[label] text`

## Usage Examples

### Fetch Phase

```cpp
auto h = tui::section_create();

struct obs : fetch_observer {
  section_handle h_;
  void on_progress(fetch_progress const& p) override {
    tui::section_set_content(h_, tui::section_frame{
      .label = "pkg@v1",
      .content = tui::progress_data{
        .percent = (p.bytes_received / (double)p.bytes_total) * 100.0,
        .status = format("%.1f MB/s", p.speed_bytes_per_sec / 1e6)
      }
    });
  }
};

obs observer{h};
fetch_sync(request, &observer);
tui::section_release(h);
```

### Build Phase

```cpp
auto h = tui::section_create();
std::vector<std::string> output;

shell_run_cfg cfg{
  .on_output_line = [&](std::string_view line) {
    output.push_back(std::string{line});

    tui::section_set_content(h, tui::section_frame{
      .label = "pkg@v1",
      .content = tui::text_stream_data{
        .lines = output,
        .line_limit = 3  // TUI shows last 3 lines
      }
    });
  }
};

shell_run(script, cfg);
tui::section_release(h);
```

### Install with Spinner

```cpp
auto h = tui::section_create();
auto start = std::chrono::steady_clock::now();

tui::section_set_content(h, tui::section_frame{
  .label = "pkg@v1",
  .content = tui::spinner_data{.text = "installing...", .start_time = start}
});

// Work happens (TUI auto-animates spinner)

tui::section_set_content(h, tui::section_frame{
  .label = "pkg@v1 ✓",
  .content = tui::static_text_data{.text = "installed"}
});

tui::section_release(h);
```

### Interactive Mode (Lua)

```lua
ctx.run([[apt-get install foo]], {interactive = true})
```

C++ integration:
```cpp
std::optional<tui::interactive_mode_guard> guard;
if (interactive) guard.emplace();
shell_run(script, cfg);
// guard destructor releases
```

## Testing

### Unit Tests

```cpp
TEST(tui, progress_bar_ansi) {
  envy::tui::test::g_terminal_width = 80;
  envy::tui::test::g_isatty = true;
  envy::tui::test::g_now = std::chrono::steady_clock::now();

  auto output = envy::tui::test::render_section_frame(section_frame{
    .label = "pkg@v1",
    .content = progress_data{.percent = 50.0, .status = "downloading"}
  });

  EXPECT_TRUE(output.find("[pkg@v1]") != std::string::npos);
  EXPECT_TRUE(output.find("50.0%") != std::string::npos);
  EXPECT_TRUE(output.find("[====") != std::string::npos);  // Bar
}

TEST(tui, spinner_animation) {
  auto start = std::chrono::steady_clock::now();

  envy::tui::test::g_now = start;
  auto out0 = envy::tui::test::render_section_frame(/*...*/);
  EXPECT_TRUE(out0.find("| installing") != std::string::npos);

  envy::tui::test::g_now = start + std::chrono::milliseconds{100};
  auto out1 = envy::tui::test::render_section_frame(/*...*/);
  EXPECT_TRUE(out1.find("/ installing") != std::string::npos);
}
```

### Functional Tests

```python
def test_ansi_rendering():
    result = subprocess.run(['./out/build/envy', 'sync'],
                           stderr=subprocess.PIPE,
                           env={'TERM': 'xterm-256color'})
    assert '\x1b[' in result.stderr.decode()  # ANSI codes present

def test_fallback_mode():
    result = subprocess.run(['./out/build/envy', 'sync'],
                           stderr=subprocess.PIPE,
                           env={'TERM': 'dumb'})
    assert '\x1b[' not in result.stderr.decode()  # No ANSI
```

## Implementation Tasks

Flat list, tests immediately after feature bringup:

- ✓ Add `progress_data`, `text_stream_data`, `spinner_data`, `static_text_data` structs to `src/tui.h`
- ✓ Add `section_frame` struct to `src/tui.h`
- ✓ Add `section_handle` typedef to `src/tui.h`
- ✓ Add `section_create()`, `section_set_content()`, `section_release()` declarations to `src/tui.h`
- ✓ Add `acquire_interactive_mode()`, `release_interactive_mode()` declarations to `src/tui.h`
- ✓ Add `interactive_mode_guard` class declaration to `src/tui.h`
- ✓ Add `section_state` struct to `src/tui.cpp`
- ✓ Add `tui_progress_state` struct to `src/tui.cpp`
- ✓ Add global `s_progress` to `src/tui.cpp`
- ✓ Add `#ifdef ENVY_UNIT_TEST` test backdoor globals (`g_terminal_width`, `g_isatty`, `g_now`) to `src/tui.cpp`
- ✓ Implement `get_terminal_width()` in `src/tui.cpp` (POSIX `ioctl`, Windows `GetConsoleScreenBufferInfo`, test backdoor)
- ✓ Implement `is_ansi_supported()` in `src/tui.cpp` (TTY check, `TERM` check, Windows VT, test backdoor)
- ✓ Implement `get_now()` in `src/tui.cpp` (with test backdoor)
- ✓ Implement `render_progress_bar()` in `src/tui.cpp`
- ✓ Add unit test for `render_progress_bar()` ANSI mode (80 cols, verify bar, percent, label)
- ✓ Add unit test for `render_progress_bar()` with different percentages (0%, 50%, 100%)
- ✓ Implement `render_text_stream()` in `src/tui.cpp`
- ✓ Add unit test for `render_text_stream()` with 0 lines
- ✓ Add unit test for `render_text_stream()` with 1 line
- ✓ Add unit test for `render_text_stream()` with multiple lines
- ✓ Implement `render_spinner()` in `src/tui.cpp` (compute frame from elapsed time)
- ✓ Add unit test for `render_spinner()` animation frames (0ms, 100ms, 200ms, 300ms → 4 frames)
- ✓ Add unit test for `render_spinner()` frame wrapping (400ms → frame 0 again)
- ✓ Implement `render_static_text()` in `src/tui.cpp`
- ✓ Add unit test for `render_static_text()` (verify label and text)
- ✓ Implement `render_section_frame()` dispatcher in `src/tui.cpp` (variant visitor)
- ✓ Add unit test for `render_section_frame()` with each variant type
- ✓ Implement `render_section_frame_fallback()` in `src/tui.cpp`
- ✓ Add unit test for `render_section_frame_fallback()` progress (no bar, just percent)
- ✓ Add unit test for `render_section_frame_fallback()` spinner (dots instead of frames)
- ✓ Add unit test for `render_section_frame_fallback()` text stream (immediate print)
- ✓ Add `#ifdef ENVY_UNIT_TEST` test namespace with `render_section_frame()` wrapper in `src/tui.cpp`
- ✓ Implement `section_create()` in `src/tui.cpp` (allocate handle, push to vector)
- ✓ Implement `section_set_content()` in `src/tui.cpp` (find by handle, update cached_frame)
- ✓ Implement `section_release()` in `src/tui.cpp` (find by handle, mark inactive)
- ✓ Add unit test for `section_create()` (returns valid handle)
- ✓ Add unit test for `section_set_content()` (updates frame)
- ✓ Add unit test for `section_release()` (marks inactive)
- ✓ Add unit test for `section_set_content()` on released section (no-op)
- ✓ Add unit test for `section_set_content()` on invalid handle (no-op)
- ✓ Add unit test for concurrent `section_create()` from multiple threads (no race)
- ✓ Add unit test for concurrent `section_set_content()` from multiple threads (no race)
- ✓ Adjust `kRefreshIntervalMs` to 33ms in `src/tui.cpp`
- ✓ Implement `render_ansi_frame()` in `src/tui.cpp` (clear previous, render all active, count lines)
- ✓ Implement `render_fallback_frame()` in `src/tui.cpp` (throttle 2s, print if changed)
- ✓ Modify `worker_thread()` in `src/tui.cpp` to: clear progress, flush logs, then render new progress (correct ordering)
- ✓ Add unit test for line counting in multi-line output (text stream with 3 lines = 4 lines total)
- ✓ Add unit test for inactive sections not rendering (create, release, verify not in output)
- ✓ Implement `pause_rendering()` in `src/tui.cpp` (clear ANSI region if present)
- ✓ Implement `resume_rendering()` in `src/tui.cpp` (no-op)
- ✓ Implement `acquire_interactive_mode()` in `src/tui.cpp` (lock mutex, call pause)
- ✓ Implement `release_interactive_mode()` in `src/tui.cpp` (call resume, unlock mutex)
- ✓ Implement `interactive_mode_guard` constructor in `src/tui.cpp` (call acquire)
- ✓ Implement `interactive_mode_guard` destructor in `src/tui.cpp` (call release)
- ✓ Add unit test for `interactive_mode_guard` RAII (verify lock/unlock)
- ✓ Add unit test for `interactive_mode_guard` exception safety (unlock on throw)
- ✓ Add unit test for serialized interactive mode (two threads, second blocks until first releases)
- ✓ Build with `./build.sh` (verify no compile errors after core TUI changes)
- ✓ Run unit tests `./out/build/envy_unit_tests` (verify all TUI tests pass)
- ✓ Add `interactive` option parsing to `make_ctx_run()` in `src/lua_ctx/lua_ctx_run.cpp` (after line 132)
- ✓ Wrap `shell_run()` call with `std::optional<tui::interactive_mode_guard>` in `src/lua_ctx/lua_ctx_run.cpp`
- ✓ Build with `./build.sh` (verify no compile errors after Lua changes)
- ✓ Run unit tests `./out/build/envy_unit_tests` (verify no regressions)
- ✓ Integrate progress bar in `phase_fetch.cpp`: create section in recipe, update on progress callbacks
- ✓ Build with `./build.sh` (verify no compile errors after fetch integration)
- ✓ Manual test: run `envy sync` with package requiring fetch, verify progress bar visible and animates
- ✓ Integrate progress bar in `phase_stage.cpp`: create section, update via `archive_extract_observer`, release after
- ✓ Build with `./build.sh` (verify no compile errors after stage integration)
- ✓ Manual test: run `envy sync` with package requiring extraction, verify progress bar visible
- ✓ Integrate text stream in `phase_build.cpp`: create section via `tui_actions::run_progress`, update on output lines, show last 3 lines
- ✓ Build with `./build.sh` (verify no compile errors after build integration)
- ✓ Manual test: run `envy sync` with package requiring build, verify last 3 lines of output visible
- ✓ Refactor `phase_build.cpp`: string-returning BUILD functions route through `ctx.run()`, auto-TUI integration via `run_progress`
- ✓ Migrate `phase_check.cpp` `run_check_string()`: replace direct `shell_run()` + `tui::info()` callbacks with `run_progress` tracker (or route through `ctx.run()` if in Lua context)
- ✓ Migrate `phase_install.cpp` `run_shell_install()`: replace direct `shell_run()` + `tui::info()` callbacks with `run_progress` tracker (pattern: create tracker, call `on_command_start()`, pass `on_output_line` callback to `shell_run_cfg`)
- ✓ Build with `./build.sh` (verify no compile errors after check/install migration)
- Manual test: run `envy sync` with `brew_package` (verify brew output appears in `[local.brew_package@r0]` section, not message log)
- ✓ Add functional test (Python): parallel recipes complete successfully (ANSI verification requires manual testing with real TTY)
- ✓ Add functional test (Python): fallback mode with `TERM=dumb` (verify no `\x1b[` in stderr)
- ✓ Add functional test (Python): fallback mode with piped stderr `2>&1 | cat` (verify no `\x1b[`)
- ✓ Run functional tests `python3.13 -m functional_tests.test_tui` (all tests pass)
- Manual test: interactive mode (create recipe with `interactive=true`, verify progress clears) - automated testing requires PTY allocation
- Manual test: run `envy sync` in normal terminal (verify ANSI progress sections render and animate)
- Manual test: run `TERM=dumb envy sync` (verify fallback text-only progress)
- Manual test: run `envy sync 2>&1 | cat` (verify fallback mode, no ANSI codes)
- Manual test: run `envy sync` with recipe requiring `interactive=true` (verify progress clears, subprocess owns terminal)
- Manual test: run `envy sync` with 10+ parallel recipes (verify stable ordering, no flicker)

## Implementation Summary

**Modified files**:
- `src/tui.h` — Section frame types, section API, interactive mode API
- `src/tui.cpp` — Section state, pure render functions, worker thread integration
- `src/tui_tests.cpp` — Unit tests for render functions and lifecycle
- `src/tui_actions.h` — Progress tracker classes (`run_progress`, `fetch_progress_tracker`, `extract_progress_tracker`, etc.)
- `src/tui_actions.cpp` — Centralized TUI management for common operations
- `src/lua_ctx/lua_ctx_run.cpp` — Auto-creates `run_progress` when recipe has `tui_section`, `interactive` option, guard wrapper
- `src/phases/phase_build.cpp` — String-returning BUILD routes through `ctx.run()` for auto-TUI integration
- `src/phases/phase_fetch.cpp` — Uses `fetch_progress_tracker` from `tui_actions`
- `src/phases/phase_stage.cpp` — Uses `extract_progress_tracker` from `tui_actions`
- `src/phases/phase_check.cpp` — Migrated `run_check_string()` to use `run_progress` tracker
- `src/phases/phase_install.cpp` — Migrated `run_shell_install()` to use `run_progress` tracker

**Key decisions**:
1. Immediate-mode: stateless rendering, complete frame data per call
2. Terminal width per frame: syscall every 33ms
3. Test backdoor: ifdef globals (`g_terminal_width`, `g_isatty`, `g_now`)
4. Fallback throttle: 2s intervals, print if changed
5. Allocation order: render in creation order (stable, predictable)
6. Mutable labels: each frame specifies label
7. No progress bar animation: show current percent directly
8. Spinner auto-animation: TUI computes frame from `(now - start_time) / duration`
9. Centralize TUI logic: phases delegate to `tui_actions` helpers, avoid phase-specific TUI code
10. Auto-TUI for `ctx.run()`: Lua scripts get TUI integration automatically when calling `ctx.run()`

# Check/Install Runtime Behavior Plan

## Goals
- Quiet checks by default; surface output only on failure (string form).
- Install string/returned-string executes as shell with streaming output.
- ctx.run gains quiet/capture/exit_code controls; capture splits stdout/stderr while streaming interleaved when not quiet.
- Default shells manifest-driven (see architecture.md shell config); check uses manifest dir cwd, install uses install_dir.
- Errors include command, exit code, stdout/stderr.

## Behavior Matrix
**Check (string):** Quiet success; verbose failure prints stdout+stderr via `tui::error()`, then throws with command+exit+outputs. Manifest `default_shell`, cwd=manifest dir—user-managed packages run project-relative, cache-unaware. Silent success even with `--verbose` (only phase transitions log).

**Check (ctx.run):** Honors quiet/capture—defaults stream to TUI, throw on error. Returns a table with `exit_code` (default) or `stdout, stderr, exit_code` when capture is enabled. Signature remains `ctx.run("script", opts?)`.

**Install (string or function→string):** Shell exec in install_dir, streams to TUI, throws on error, marks complete on zero. Functions may call ctx.run() *and* return string—natural execution order (run() immediate, mirrors fetch). Returned strings spawn fresh shell with same defaults (manifest shell, install_dir cwd)—use ctx.run() for control. Nil/no return OK; non-string/non-nil errors.

**ctx.run:** quiet (silent, still throws), capture (split stdout/stderr into return table, streams interleaved to TUI when !quiet), neither (streams interleaved, throws on error, returns exit_code). Signature: `ctx.run("script", opts_table?)`.

**Examples:**
```lua
-- Check with string (quiet success, loud failure)
check = "python3 --version"

-- Check with ctx.run (explicit control)
check = function(ctx)
  local res = ctx.run("brew list")  -- Streams, throws on error, returns table with exit_code
  if res.exit_code ~= 0 then error("brew not installed") end
end

-- Install with string
install = "make install"

-- Install with ctx.run + returned string (both execute)
install = function(ctx)
  ctx.run("make -j8")     -- Build with 8 cores
  return "make install"   -- Fresh shell, runs in install_dir
end

-- Capture stdout/stderr (table fields when capture=true)
check = function(ctx)
  local res = ctx.run("python3 --version", {capture=true})
  if res.exit_code ~= 0 then error("python failed") end
  if not res.stdout:match("3%.11") then
    error("Wrong Python version: " .. res.stdout)
  end
end

-- Ignore some fields
local out = ctx.run("git rev-parse HEAD", {capture=true}).stdout  -- Just stdout
local res = ctx.run("make", {capture=true})
local err = res.stderr
local code = res.exit_code
```

## Implementation Tasks
- [ ] Add stdout/stderr split capture in shell_run (preserve interleaved streaming for TUI; best-effort ordering due to OS pipe buffering).
- [ ] Wire ctx.run options (quiet, capture); return table with `exit_code` (default) or `stdout, stderr, exit_code` when capture=true.
- [ ] Make check string path quiet-on-success, verbose-on-failure via `tui::error()`; ensure manifest default shell/cwd.
- [ ] Run install string/returned-string with streaming output; spawn fresh shell (manifest defaults) for returned strings; validate return type (string/nil only); mark complete only for cache-managed installs.
- [ ] Update failure messages (command, exit code, full stdout/stderr—no truncation).
- [ ] Refresh docs/tests as needed.

## Tests (exhaustive functional/unit)
- [ ] Check string success: no TUI output (silent even with `--verbose`).
- [ ] Check string failure: stdout+stderr printed via `tui::error()`, error includes command+exit+outputs.
- [ ] Check with ctx.run quiet=true success/failure (no TUI, returns exit_code, throws on non-zero).
- [ ] Check with ctx.run capture=true: tuple `stdout, stderr, exit_code` returned; streams when quiet=false.
- [ ] Check with ctx.run capture=false: returns exit_code scalar.
- [ ] Install string success: streams to TUI, marks complete.
- [ ] Install string failure: streams, throws with full outputs (no truncation).
- [ ] Install function calling ctx.run() and returning string: both execute in order, returned string spawns fresh shell.
- [ ] Install function returning string success/failure paths.
- [ ] Install function returning non-string/non-nil → error; nil/no return → no extra action.
- [ ] ctx.run default (no flags): streams interleaved, throws on non-zero, returns exit_code.
- [ ] ctx.run quiet + capture combos across success/failure (4 combinations: !q!c, q!c, !qc, qc).
- [ ] Default shell (manifest `default_shell`) respected in check/install string paths.
- [ ] Check cwd = manifest directory (test via relative path in check string).
- [ ] Install cwd = install_dir (test via relative path in install string and returned string).
- [ ] Tuple unpacking patterns: `local out, err, code = ctx.run(..., capture=true)`, `local out = ctx.run(..., capture=true)` (ignores rest).
- [ ] Shell error types: command not found (exit 127), syntax error, timeout/signal (if exposed).
- [ ] Concurrent large stdout+stderr with capture (no pipe deadlock, interleaving best-effort).
- [ ] Empty outputs in failure messages (clarify in error, not blank lines).
- [ ] Check string with cd in function body, returned string cwd unchanged (fresh shell isolation).

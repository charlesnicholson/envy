# Check/Install Runtime Behavior Plan

## Goals
- Quiet checks by default; surface output only on failure (string form).
- Install string/returned-string executes as shell with streaming output.
- ctx.run gains quiet/capture/exit_code controls; capture splits stdout/stderr while streaming interleaved when not quiet.
- Default shells manifest-driven (see architecture.md shell config); check uses manifest dir cwd, install uses install_dir (cache-managed) or tmp_dir (user-managed).
- Errors include command, exit code, stdout/stderr.
- User-managed packages (with check verb) cannot use cache phases (fetch/stage/build) or cache directory paths—only check + install phases with ephemeral tmp_dir.

## Behavior Matrix
**Check (string):** Quiet success; verbose failure prints stdout+stderr via `tui::error()`, then throws with command+exit+outputs. Manifest `default_shell`, cwd=manifest dir—user-managed packages run project-relative, cache-unaware. Silent success even with `--verbose` (only phase transitions log).

**Check (ctx.run):** Honors quiet/capture—defaults stream to TUI, throw on error. Returns table with `exit_code` field (capture=false) or `stdout, stderr, exit_code` fields (capture=true). Signature: `ctx.run("command [args...]", opts_table?)` — command string executed via shell.

**Install (string or function→string):** Shell exec in install_dir (cache-managed) or tmp_dir (user-managed), streams to TUI, throws on error, marks complete on zero (cache-managed only; user-managed skip marking, rely on check verb). Functions may call ctx.run() *and* return string—natural execution order (run() immediate, mirrors fetch). Returned strings spawn fresh shell with same defaults (manifest shell, install_dir/tmp_dir cwd)—use ctx.run() for control. Nil/no return OK; non-string/non-nil errors.

**ctx.run:** quiet (silent, still throws), capture (split stdout/stderr into return table fields, streams interleaved to TUI when !quiet), neither (streams interleaved, throws on error, returns table with exit_code field). Always returns table; capture controls which fields are populated.

**User-Managed Package Model:**
- **Allowed phases**: recipe_fetch, check, install, deploy (note: recipe_fetch is distinct from fetch; fetch/stage/build phases forbidden)
- **ctx API**: tmp_dir (ephemeral), run(), options, identity, asset()
- **Forbidden ctx**: fetch_dir, stage_dir, build_dir, install_dir, asset_dir, fetch(), extract_all(), mark_install_complete()
- **Validation**: Parse error if check verb + fetch/stage/build phases declared; runtime error if mark_install_complete() called
- **Workspace lifecycle**: tmp_dir created during install, entire entry_dir deleted after completion (no cache persistence)

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

-- User-managed package (system wrapper)
check = function(ctx)
  return ctx.run("python3 --version", {quiet=true}).exit_code == 0
end

install = function(ctx)
  -- tmp_dir available for ephemeral workspace
  local function shell_escape(str)
    return "'" .. tostring(str):gsub("'", "'\\''") .. "'"
  end

  ctx.run("echo 'export PYTHONPATH=/usr/local/lib' > " ..
          shell_escape(ctx.tmp_dir .. "/.pythonrc"))

  if ENVY_PLATFORM == "darwin" then
    ctx.run("brew install python3")
    ctx.run("cp " .. shell_escape(ctx.tmp_dir .. "/.pythonrc") .. " ~/.pythonrc")
  elseif ENVY_PLATFORM == "linux" then
    -- Use appropriate package manager with sudo; Debian/Ubuntu shown here
    ctx.run("sudo apt-get install -y python3")
    -- Fedora: sudo dnf install python3
    -- Arch: sudo pacman -S python
    -- Alpine: sudo apk add python3
  end
  -- No ctx.mark_install_complete() call
  -- tmp_dir deleted after install completes
end
```

## Implementation Tasks
- [ ] Add stdout/stderr split capture in shell_run (preserve interleaved streaming for TUI; best-effort ordering due to OS pipe buffering).
- [ ] Wire ctx.run options (quiet, capture); always return table—`exit_code` field only (capture=false) or `stdout, stderr, exit_code` fields (capture=true).
- [ ] Make check string path quiet-on-success, verbose-on-failure via `tui::error()`; ensure manifest default shell/cwd.
- [ ] Run install string/returned-string with streaming output; spawn fresh shell (manifest defaults) for returned strings; validate return type (string/nil only); mark complete only for cache-managed recipes (skip for user-managed).
- [ ] User-managed package validation: parse error if check verb + fetch/stage/build phases declared; runtime error if mark_install_complete() called (existing).
- [ ] User-managed ctx isolation: expose tmp_dir (install phase only), run(), options, identity, asset(); hide fetch_dir, stage_dir, build_dir, install_dir, asset_dir, fetch(), extract_all(), mark_install_complete().
- [ ] User-managed install cwd: use tmp_dir instead of install_dir for string/returned-string execution.
- [ ] Update failure messages (command, exit code, full stdout/stderr—no truncation).
- [ ] Refresh docs/tests as needed.

## Tests (exhaustive functional/unit)
- [ ] Check string success: no TUI output (silent even with `--verbose`).
- [ ] Check string failure: stdout+stderr printed via `tui::error()`, error includes command+exit+outputs.
- [ ] Check with ctx.run quiet=true success/failure (no TUI, returns table with exit_code, throws on non-zero).
- [ ] Check with ctx.run capture=true: table with `stdout, stderr, exit_code` fields returned; streams when quiet=false.
- [ ] Check with ctx.run capture=false: returns table with only exit_code field.
- [ ] Install string success (cache-managed): streams to TUI, marks complete, cwd=install_dir.
- [ ] Install string success (user-managed): streams to TUI, does not mark complete, cwd=tmp_dir.
- [ ] Install string failure: streams, throws with full outputs (no truncation).
- [ ] Install function calling ctx.run() and returning string: both execute in order, returned string spawns fresh shell.
- [ ] Install function returning string success/failure paths.
- [ ] Install function returning non-string/non-nil → error; nil/no return → no extra action.
- [ ] ctx.run default (no flags): streams interleaved, throws on non-zero, returns table with exit_code field.
- [ ] ctx.run quiet + capture combos across success/failure (4 combinations: !q!c, q!c, !qc, qc).
- [ ] Default shell (manifest `default_shell`) respected in check/install string paths.
- [ ] Check cwd = manifest directory (test via relative path in check string).
- [ ] Install cwd = install_dir (cache-managed) or tmp_dir (user-managed) via relative path in install string and returned string.
- [ ] Table field access patterns: `res.exit_code`, `res.stdout`, chained access `ctx.run(...).stdout`.
- [ ] Shell error types: command not found (exit 127), syntax error, timeout/signal (if exposed).
- [ ] Concurrent large stdout+stderr with capture (no pipe deadlock, interleaving best-effort).
- [ ] Empty outputs in failure messages (clarify in error, not blank lines).
- [ ] Install function with ctx.run() + returned string: returned string cwd=install_dir/tmp_dir (fresh shell isolation).
- [ ] User-managed parse error: check verb + fetch phase declared → error with message.
- [ ] User-managed parse error: check verb + stage phase declared → error.
- [ ] User-managed parse error: check verb + build phase declared → error.
- [ ] User-managed runtime error: check verb + ctx.mark_install_complete() called → error (existing test).
- [ ] User-managed ctx isolation: tmp_dir accessible in install, fetch_dir/stage_dir/build_dir/install_dir/asset_dir return error or nil.
- [ ] User-managed ctx isolation: ctx.fetch(), ctx.extract_all() not exposed (runtime error if called).
- [ ] User-managed workspace lifecycle: tmp_dir created during install, entry_dir deleted after completion.
- [ ] User-managed check phase: no tmp_dir exposed (check tests system state only).
- [ ] User-managed install uses tmp_dir for downloads/temp files (test via ctx.tmp_dir path).

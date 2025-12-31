# envy.* Lua API Reference

Complete reference for the `envy.*` namespace available to spec scripts.

## Platform Constants

```lua
envy.PLATFORM      -- "darwin" | "linux" | "windows"
envy.ARCH          -- "arm64" | "aarch64" | "x86_64"
envy.PLATFORM_ARCH -- e.g., "darwin-arm64", "linux-x86_64"
envy.EXE_EXT       -- "" on Unix, ".exe" on Windows
```

## Shell Constants

```lua
ENVY_SHELL.BASH       -- Unix only; error on Windows
ENVY_SHELL.SH         -- Unix only; error on Windows
ENVY_SHELL.CMD        -- Windows only; error on Unix
ENVY_SHELL.POWERSHELL -- Windows only; error on Unix
```

Platform-incompatible shells throw at runtime; all constants exist on all platforms.

---

## Logging

```lua
envy.trace(msg)  -- Debug trace (verbose mode)
envy.debug(msg)  -- Debug output
envy.info(msg)   -- Informational
envy.warn(msg)   -- Warning
envy.error(msg)  -- Error (does not throw)
envy.stdout(msg) -- Direct stdout (bypasses TUI)
```

---

## Command Execution

### envy.run(script, [opts]) → table

Execute shell script in phase context.

**Arguments:**
- `script` — string or array of strings (joined with newlines)
- `opts` — optional table:
  - `cwd` — working directory (default: phase's run_dir)
  - `env` — table of environment variable overrides
  - `shell` — `ENVY_SHELL.*` constant
  - `quiet` — suppress output (default: false)
  - `capture` — capture stdout/stderr (default: false)
  - `check` — throw on non-zero exit (default: false)
  - `interactive` — enable TTY passthrough (default: false)

**Returns:** `{ exit_code, stdout, stderr }`

```lua
-- Simple command
envy.run("make -j$(nproc)")

-- With options
local result = envy.run("./configure --prefix=" .. install_dir, {
  cwd = "subdir",
  env = { CC = "clang" },
  capture = true,
  check = true
})
print(result.stdout)

-- Multi-line script
envy.run({
  "cmake -B build -G Ninja",
  "cmake --build build",
  "cmake --install build"
}, { check = true })
```

**Default cwd by phase:**
- FETCH: tmp_dir
- STAGE/BUILD: stage_dir
- INSTALL: install_dir (cache-managed) or project_root (user-managed)
- CHECK: project_root

---

## File Operations

All relative paths resolve against phase's run_dir.

### envy.copy(src, dst)
Copy file or directory recursively; creates parent directories.

### envy.move(src, dst)
Move/rename file or directory.

### envy.remove(path)
Delete file or directory recursively.

### envy.exists(path) → bool
Check if path exists.

### envy.is_file(path) → bool
Check if path is regular file.

### envy.is_dir(path) → bool
Check if path is directory.

```lua
if not envy.exists("src") then
  envy.copy(fetch_dir .. "/upstream-src", "src")
end
envy.move("output/lib", install_dir .. "/lib")
```

---

## Archive Extraction

### envy.extract(archive_path, dest_dir, [opts]) → int

Extract single archive; returns file count.

**Options:**
- `strip` — components to strip from paths (default: 0)

```lua
envy.extract(fetch_dir .. "/source.tar.gz", ".", { strip = 1 })
```

### envy.extract_all(src_dir, dest_dir, [opts])

Extract all archives in directory.

```lua
envy.extract_all(fetch_dir, stage_dir, { strip = 1 })
```

---

## Fetch Operations

### envy.fetch(source, opts) → string|table

Download files to destination.

**source formats:**
- `"https://example.com/file.tar.gz"` — URL string
- `{ source = "...", sha256 = "..." }` — single spec with optional hash
- `{ { source = "..." }, { source = "..." } }` — array of specs

**opts:**
- `dest` — required, destination directory

**Returns:** basename(s) of downloaded files.

```lua
-- Single file
local name = envy.fetch("https://example.com/file.tar.gz", { dest = tmp_dir })

-- With hash verification
envy.fetch({ source = url, sha256 = "abc123..." }, { dest = tmp_dir })

-- Multiple files (parallel download)
local files = envy.fetch({
  { source = url1, sha256 = hash1 },
  { source = url2, sha256 = hash2 }
}, { dest = tmp_dir })
```

### envy.commit_fetch(files)

Move verified files from tmp_dir to fetch_dir. FETCH phase only.

**files formats:**
- `"filename"` — single file, no hash check
- `{ filename = "...", sha256 = "..." }` — single file with hash
- `{ "file1", "file2" }` — array of filenames
- `{ { filename = "...", sha256 = "..." }, ... }` — array with hashes

```lua
FETCH = function(tmp_dir, options)
  envy.fetch(url, { dest = tmp_dir })
  envy.commit_fetch({ filename = "file.tar.gz", sha256 = expected_hash })
end
```

### envy.verify_hash(file_path, expected_sha256) → bool

Verify file SHA256 hash; returns true if match, false otherwise.

```lua
if not envy.verify_hash(file, hash) then
  error("Hash mismatch!")
end
```

---

## Dependency Access

### envy.asset(identity) → string

Get installed asset path for declared dependency.

**Requirements:**
- Caller must have strong dependency on `identity`
- Access must occur at or after dependency's `needed_by` phase

```lua
BUILD = function(stage_dir, fetch_dir, tmp_dir, options)
  local gcc = envy.asset("arm.gcc@v2")
  envy.run("./configure CC=" .. gcc .. "/bin/arm-none-eabi-gcc")
end
```

### envy.product(name) → string

Get named product value from provider dependency.

```lua
-- Spec declares: NEEDS_PRODUCTS = { python_path = { needed_by = "build" } }
BUILD = function(...)
  local python = envy.product("python_path")
  envy.run(python .. " setup.py build")
end
```

---

## Path Utilities

### envy.path.join(...) → string
Join path components.

### envy.path.basename(path) → string
Extract filename with extension.

### envy.path.dirname(path) → string
Extract parent directory.

### envy.path.stem(path) → string
Extract filename without extension.

### envy.path.extension(path) → string
Extract file extension (with dot).

```lua
local p = envy.path.join(stage_dir, "bin", "tool" .. envy.EXE_EXT)
local ext = envy.path.extension("archive.tar.gz")  -- ".gz"
```

---

## Template Substitution

### envy.template(str, values) → string

Mustache-style `{{placeholder}}` substitution.

```lua
local cmd = envy.template(
  "./configure --prefix={{prefix}} CC={{cc}}",
  { prefix = install_dir, cc = "clang" }
)
```

---

## Thread-Local Context

The `envy.*` functions operate on thread-local phase context set by C++ via `phase_context_guard` RAII:

```cpp
phase_context_guard ctx_guard{ &engine, recipe, run_dir };
// Lua code called here can use envy.run(), envy.asset(), etc.
```

Context provides:
- Current spec pointer (for `envy.asset` dependency validation)
- Engine pointer (for TUI progress tracking)
- Run directory (default cwd for `envy.run` and file operations)

Functions requiring context throw if called outside phase execution.

---

## Migration from ctx.* (deprecated)

| Old API | New API |
|---------|---------|
| `ctx:run(cmd)` | `envy.run(cmd)` |
| `ctx:extract(...)` | `envy.extract(...)` |
| `ctx:copy(...)` | `envy.copy(...)` |
| `ctx:move(...)` | `envy.move(...)` |
| `ctx:asset(id)` | `envy.asset(id)` |
| `ctx:product(name)` | `envy.product(name)` |
| `ctx.stage_dir` | Use `stage_dir` function parameter |
| `ctx.fetch_dir` | Use `fetch_dir` function parameter |
| `ctx.install_dir` | Use `install_dir` function parameter |

The new API passes directories as function parameters instead of context fields:

```lua
-- Old
BUILD = function(ctx)
  ctx:run("make DESTDIR=" .. ctx.stage_dir)
end

-- New
BUILD = function(stage_dir, fetch_dir, tmp_dir, options)
  envy.run("make DESTDIR=" .. stage_dir)
end
```

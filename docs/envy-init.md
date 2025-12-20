# envy init Command Implementation

## Overview

The `envy init` command bootstraps a new project with envy, creating the manifest, copying the envy binary, extracting lua_ls type definitions, and generating IDE configuration. This enables zero-install deployment: users download a single envy binary and run `envy init` to set up everything needed for a project.

## Command Signature

```
envy init <project-dir> <bin-dir>
```

- `project-dir`: Where to create the manifest (envy.lua) and IDE config (.luarc.json)
- `bin-dir`: Where to place the envy binary and type definitions

## File Layout After Execution

```
<project-dir>/
  envy.lua              # Manifest template
  .luarc.json           # lua_ls configuration

<bin-dir>/
  envy                  # Binary copy (envy.exe on Windows)
  types/
    envy.lua            # lua_ls type definitions
    .version            # BLAKE3 hash for upgrade detection
```

## Design Decisions

### Single Type Definition File

All lua_ls annotations live in one file (`envy.lua` with `---@meta` header). This simplifies versioning, extraction, and upgrade detection. The file is machine-generated and users should not edit it—running `envy init` again after upgrading envy will overwrite with the new version.

### Compression with zstd

The type definitions are zstd-compressed at build time and embedded in the binary as a C++ byte array. At runtime, `envy init` decompresses and writes to disk. This minimizes binary size while leveraging envy's existing zstd dependency.

### BLAKE3 Hash for Version Detection

The uncompressed content's BLAKE3 hash is embedded alongside the compressed data. On extraction, envy checks if `<bin-dir>/types/.version` matches the embedded hash—if so, extraction is skipped. This makes repeated `envy init` calls idempotent and upgrades automatic.

### Relative Paths in .luarc.json

The generated `.luarc.json` uses a relative path to `<bin-dir>/types/`, making the configuration portable across machines. When the project is cloned, lua_ls finds the types without user-specific absolute paths.

## Build-Time Embedding

### CMake Resource Embedding

A new CMake function `embed_resource()` handles:
1. Reading the source Lua file
2. Computing BLAKE3 hash of uncompressed content
3. Compressing with zstd
4. Generating a C++ header with constexpr byte arrays

The generated header (`${CMAKE_BINARY_DIR}/generated/envy_types_data.h`) contains:

```cpp
#pragma once
#include <cstdint>
#include <cstddef>

constexpr char kEnvyTypesHash[] = "...64-char-hex...";
constexpr std::uint8_t kEnvyTypesCompressed[] = { 0x28, 0xb5, 0x2f, ... };
constexpr std::size_t kEnvyTypesCompressedSize = ...;
constexpr std::size_t kEnvyTypesUncompressedSize = ...;
```

Similarly, the manifest template is embedded (though uncompressed, as it's small).

### Embedding Script

The embedding uses a Python script (`cmake/scripts/embed_resource.py`) invoked at configure time via `execute_process()`. The script:
1. Reads input file
2. Computes BLAKE3 hash using envy's blake3 utility or Python fallback
3. Compresses with zstd (via command-line tool or Python binding)
4. Outputs C++ header with hex-encoded byte array

## Runtime Extraction

### Executable Path Detection

To copy itself, envy needs to find its own executable path. This requires platform-specific code:

- **Linux**: Read `/proc/self/exe` symlink
- **macOS**: Call `_NSGetExecutablePath()` then `realpath()`
- **Windows**: Call `GetModuleFileNameW(NULL, ...)`

This functionality lives in `src/util/exe_path.{h,cpp}`.

### Extraction Logic

```cpp
void extract_types(std::filesystem::path const& types_dir) {
  auto version_file = types_dir / ".version";

  // Check if already up-to-date
  if (std::filesystem::exists(version_file)) {
    std::string existing = read_file_to_string(version_file);
    if (existing == kEnvyTypesHash) {
      tui::info("Types already up-to-date");
      return;
    }
  }

  // Decompress
  std::vector<char> uncompressed(kEnvyTypesUncompressedSize);
  size_t result = ZSTD_decompress(
    uncompressed.data(), kEnvyTypesUncompressedSize,
    kEnvyTypesCompressed, kEnvyTypesCompressedSize);

  if (ZSTD_isError(result)) {
    throw std::runtime_error("Failed to decompress types: " +
                             std::string(ZSTD_getErrorName(result)));
  }

  // Write files
  std::filesystem::create_directories(types_dir);
  write_file(types_dir / "envy.lua", uncompressed);
  write_file(version_file, kEnvyTypesHash);
}
```

### Init Command Flow

```cpp
bool cmd_init::execute() {
  // 1. Validate and create directories
  std::filesystem::create_directories(cfg_.project_dir);
  std::filesystem::create_directories(cfg_.bin_dir);

  // 2. Create manifest template (skip if exists)
  auto manifest = cfg_.project_dir / "envy.lua";
  if (!std::filesystem::exists(manifest)) {
    write_file(manifest, kManifestTemplate);
    tui::info("Created %s", manifest.string().c_str());
  } else {
    tui::info("Manifest already exists: %s", manifest.string().c_str());
  }

  // 3. Copy envy binary
  auto self_path = get_executable_path();
  auto dest_binary = cfg_.bin_dir / executable_name();  // "envy" or "envy.exe"
  std::filesystem::copy_file(self_path, dest_binary,
                             std::filesystem::copy_options::overwrite_existing);
  tui::info("Copied envy to %s", dest_binary.string().c_str());

  // 4. Extract type definitions
  extract_types(cfg_.bin_dir / "types");
  tui::info("Extracted type definitions");

  // 5. Generate .luarc.json
  auto luarc = cfg_.project_dir / ".luarc.json";
  auto rel_types = std::filesystem::relative(cfg_.bin_dir / "types", cfg_.project_dir);
  write_luarc_json(luarc, rel_types.string());
  tui::info("Created %s", luarc.string().c_str());

  return true;
}
```

## Type Definition Content

The embedded `envy_types.lua` provides complete lua_ls annotations for:

**Global Constants**
- `ENVY_SHELL.BASH`, `ENVY_SHELL.SH`, `ENVY_SHELL.CMD`, `ENVY_SHELL.POWERSHELL`

**envy Namespace**
- Platform: `envy.PLATFORM`, `envy.ARCH`, `envy.PLATFORM_ARCH`, `envy.EXE_EXT`
- Logging: `envy.trace()`, `envy.debug()`, `envy.info()`, `envy.warn()`, `envy.error()`, `envy.stdout()`
- Path utilities: `envy.path.join()`, `envy.path.basename()`, `envy.path.dirname()`, `envy.path.stem()`, `envy.path.extension()`
- File operations: `envy.copy()`, `envy.move()`, `envy.remove()`, `envy.exists()`, `envy.is_file()`, `envy.is_dir()`
- Execution: `envy.run()` with `RunOptions` and `RunResult` types
- Archives: `envy.extract()`, `envy.extract_all()` with `ExtractOptions`
- Fetch: `envy.fetch()`, `envy.commit_fetch()`, `envy.verify_hash()` with `FetchSpec`, `FetchOptions`
- Dependencies: `envy.asset()`, `envy.product()`
- Template: `envy.template()`

**Manifest Globals**
- `IDENTITY`, `PACKAGES`, `DEPENDENCIES`, `PRODUCTS`, `OPTIONS`, `DEFAULT_SHELL`

**Phase Functions**
- `FETCH`, `STAGE`, `BUILD`, `INSTALL`, `CHECK` with union types for declarative forms

## .luarc.json Content

```json
{
  "$schema": "https://raw.githubusercontent.com/LuaLS/vscode-lua/master/setting/schema.json",
  "runtime.version": "Lua 5.4",
  "workspace.library": ["<relative-path-to-bin-dir>/types"],
  "diagnostics.globals": [
    "IDENTITY", "PACKAGES", "DEPENDENCIES", "PRODUCTS", "OPTIONS",
    "DEFAULT_SHELL", "FETCH", "STAGE", "BUILD", "INSTALL", "CHECK",
    "ENVY_SHELL", "envy"
  ]
}
```

## Manifest Template Content

```lua
-- envy.lua - Project manifest
-- Documentation: https://github.com/anthropics/envy

PACKAGES = {
  -- Example: "python" uses local.python recipe
  -- ["python"] = { identity = "local.python@v1" },
}
```

## CLI Registration

Following the existing command pattern with CLI11:

```cpp
// src/cli.cpp
cmd_init::cfg init_cfg{};
auto *init_cmd{ app.add_subcommand("init", "Initialize new project with envy") };
init_cmd->add_option("project_dir", init_cfg.project_dir, "Project directory")->required();
init_cmd->add_option("bin_dir", init_cfg.bin_dir, "Binary/tools directory")->required();
init_cmd->callback([&cmd_cfg, &init_cfg] { cmd_cfg = init_cfg; });
```

## Future Context

The `envy init` command establishes the foundation for upcoming features:

1. **Deploy phase**: Will materialize product launchers (shell scripts/batch files) in bin-dir that invoke `$(./envy product xyz) "$@"`

2. **Shell hooks**: Will auto-manage PATH when entering/exiting envy project directories, adding/removing bin-dir

3. **ENVY_BIN_DIR manifest field**: Will formalize the bin-dir location in the manifest for shell hooks to discover

---

## Implementation Tasks

- [ ] Create `src/embedded/envy_types.lua` with complete lua_ls type annotations (~200 lines)
- [ ] Create `src/embedded/manifest_template.lua` with starter manifest content
- [ ] Create `cmake/scripts/embed_resource.py` to compress and generate C++ headers
- [ ] Create `cmake/EmbedResource.cmake` with embed_resource() function calling the Python script
- [ ] Update `CMakeLists.txt` to invoke embed_resource() for envy_types.lua
- [ ] Update `CMakeLists.txt` to invoke embed_resource() for manifest_template.lua (uncompressed)
- [ ] Update `CMakeLists.txt` to add generated header include path
- [ ] Create `src/util/exe_path.h` declaring get_executable_path() and executable_name()
- [ ] Create `src/util/exe_path.cpp` with Linux implementation using /proc/self/exe
- [ ] Add macOS implementation to exe_path.cpp using _NSGetExecutablePath
- [ ] Add Windows implementation to exe_path.cpp using GetModuleFileNameW
- [ ] Update `CMakeLists.txt` to add exe_path.cpp to ENVY_BASE_SOURCES
- [ ] Create `src/cmds/cmd_init.h` with cmd_init class and cfg struct
- [ ] Create `src/cmds/cmd_init.cpp` with execute() implementation
- [ ] Implement extract_types() helper in cmd_init.cpp using ZSTD_decompress
- [ ] Implement write_luarc_json() helper in cmd_init.cpp
- [ ] Update `CMakeLists.txt` to add cmd_init.cpp to ENVY_BASE_SOURCES
- [ ] Update `src/cli.h` to include cmd_init.h and add to variant
- [ ] Update `src/cli.cpp` to register init subcommand with CLI11
- [ ] Create `src/cmds/cmd_init_tests.cpp` with unit tests for the command
- [ ] Add CLI parsing tests to `src/cli_tests.cpp` for init command arguments
- [ ] Create functional test: envy init creates expected files
- [ ] Create functional test: envy init is idempotent (re-run doesn't change files)
- [ ] Create functional test: envy init with different version updates types
- [ ] Build and verify all tests pass
- [ ] Test lua_ls integration manually in VSCode or Neovim

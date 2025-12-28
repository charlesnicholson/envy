---@meta
-- lua_ls type definitions for envy
-- Generated for envy @@ENVY_VERSION@@

--------------------------------------------------------------------------------
-- Platform Constants
--------------------------------------------------------------------------------

---@class envy
---@field PLATFORM "darwin"|"linux"|"windows" Current operating system
---@field ARCH "arm64"|"x86_64"|"aarch64" CPU architecture
---@field PLATFORM_ARCH string Platform-architecture pair (e.g., "darwin-arm64")
---@field EXE_EXT ""|".exe" Executable extension ("" on Unix, ".exe" on Windows)
envy = {}

--------------------------------------------------------------------------------
-- Logging
--------------------------------------------------------------------------------

---Emit trace-level log message
---@param msg string
function envy.trace(msg) end

---Emit debug-level log message
---@param msg string
function envy.debug(msg) end

---Emit info-level log message
---@param msg string
function envy.info(msg) end

---Emit warning-level log message
---@param msg string
function envy.warn(msg) end

---Emit error-level log message
---@param msg string
function envy.error(msg) end

---Write directly to stdout (bypasses TUI)
---@param msg string
function envy.stdout(msg) end

--------------------------------------------------------------------------------
-- String Templates
--------------------------------------------------------------------------------

---Replace `{{key}}` placeholders with values from table
---@param str string Template string with `{{key}}` placeholders
---@param values table<string, any> Key-value pairs for substitution
---@return string
function envy.template(str, values) end

---Extend target array with items from one or more source arrays
---@param target any[] Target array to extend (modified in-place)
---@param ... any[] Source arrays to append
---@return any[] target The same target array (for chaining)
function envy.extend(target, ...) end

--------------------------------------------------------------------------------
-- Path Operations
--------------------------------------------------------------------------------

---@class envy.path
envy.path = {}

---Join path components with platform-appropriate separator
---@param ... string Path components
---@return string
function envy.path.join(...) end

---Extract filename with extension from path
---@param path string
---@return string
function envy.path.basename(path) end

---Extract parent directory from path
---@param path string
---@return string
function envy.path.dirname(path) end

---Extract filename without extension from path
---@param path string
---@return string
function envy.path.stem(path) end

---Extract file extension including leading dot
---@param path string
---@return string
function envy.path.extension(path) end

--------------------------------------------------------------------------------
-- File Operations
--------------------------------------------------------------------------------

---Copy file or directory (recursive). Relative paths resolve from stage_dir.
---@param src string Source path
---@param dst string Destination path
function envy.copy(src, dst) end

---Move/rename file or directory. Relative paths resolve from stage_dir.
---@param src string Source path
---@param dst string Destination path
function envy.move(src, dst) end

---Delete file or directory recursively. Relative paths resolve from stage_dir.
---@param path string Path to remove
function envy.remove(path) end

---Check if path exists
---@param path string
---@return boolean
function envy.exists(path) end

---Check if path is a regular file
---@param path string
---@return boolean
function envy.is_file(path) end

---Check if path is a directory
---@param path string
---@return boolean
function envy.is_dir(path) end

--------------------------------------------------------------------------------
-- Shell Execution
--------------------------------------------------------------------------------

---@class envy.run_opts
---@field cwd? string Working directory (absolute or relative to stage_dir)
---@field env? table<string, string> Environment variables (merged with inherited)
---@field shell? integer|envy.shell_config Shell choice (ENVY_SHELL.*) or config table
---@field quiet? boolean Suppress output (default: false)
---@field capture? boolean Capture stdout/stderr in result (default: false)
---@field check? boolean Throw on non-zero exit code (default: false)
---@field interactive? boolean Enable interactive mode for user input (default: false)

---@class envy.run_result
---@field exit_code integer Process exit code
---@field stdout? string Captured stdout (only if capture=true)
---@field stderr? string Captured stderr (only if capture=true)

---Execute shell script
---@param script string|string[] Shell script (string or array of lines)
---@param opts? envy.run_opts Execution options
---@return envy.run_result
function envy.run(script, opts) end

--------------------------------------------------------------------------------
-- Archive Extraction
--------------------------------------------------------------------------------

---@class envy.extract_opts
---@field strip? integer Strip leading path components (default: 0)

---Extract single archive to destination directory
---@param archive_path string Path to archive file
---@param dest_dir string Destination directory
---@param opts? envy.extract_opts Extraction options
---@return integer files_extracted Number of files extracted
function envy.extract(archive_path, dest_dir, opts) end

---Extract all archives in source directory to destination
---@param src_dir string Directory containing archives
---@param dest_dir string Destination directory
---@param opts? envy.extract_opts Extraction options
function envy.extract_all(src_dir, dest_dir, opts) end

--------------------------------------------------------------------------------
-- Fetch Operations
--------------------------------------------------------------------------------

---@class envy.fetch_spec
---@field source string URL or local path
---@field sha256? string Expected SHA256 hash for verification
---@field ref? string Git ref (branch, tag, commit) for git sources

---@class envy.fetch_opts
---@field dest string Destination directory (required)

---Download files to destination directory
---@param source string|envy.fetch_spec|string[]|envy.fetch_spec[] Source URL(s) or spec(s)
---@param opts envy.fetch_opts Fetch options (dest is required)
---@return string|string[] basename(s) Downloaded filename(s)
function envy.fetch(source, opts) end

---@class envy.commit_fetch_entry
---@field filename string File to commit
---@field sha256? string Expected SHA256 hash for verification

---Move verified files from tmp_dir to fetch_dir (FETCH phase only)
---@param files string|envy.commit_fetch_entry|string[]|envy.commit_fetch_entry[]
function envy.commit_fetch(files) end

---Verify file hash matches expected SHA256
---@param file_path string Path to file
---@param expected_sha256 string Expected SHA256 hash (hex string)
---@return boolean matches True if hash matches
function envy.verify_hash(file_path, expected_sha256) end

--------------------------------------------------------------------------------
-- Dependency Access
--------------------------------------------------------------------------------

---Get installed asset path for a dependency
---@param identity string Dependency identity (e.g., "namespace.name@version")
---@return string asset_path Absolute path to dependency's installed assets
function envy.asset(identity) end

---Get product value from a dependency
---@param name string Product name declared in provider's PRODUCTS
---@return string value Product value (path or arbitrary string)
function envy.product(name) end

--------------------------------------------------------------------------------
-- Shell Constants
--------------------------------------------------------------------------------

---@class ENVY_SHELL
---@field BASH integer Bash shell (Unix)
---@field SH integer POSIX sh shell (Unix)
---@field CMD integer Windows cmd.exe
---@field POWERSHELL integer PowerShell (Windows)
ENVY_SHELL = {}

---@class envy.shell_config
---@field choice? integer Shell choice (ENVY_SHELL.*)
---@field file? string Path to shell executable
---@field inline? string[] Command template array (use {} for script placeholder)

--------------------------------------------------------------------------------
-- Recipe Globals
--------------------------------------------------------------------------------

---Recipe identity in "namespace.name@version" format
---@type string
IDENTITY = ""

---Recipe dependencies array
---@alias envy.dependency { recipe: string, source?: string, needed_by?: "fetch"|"stage"|"build"|"install"|"check", product?: string, weak?: boolean, options?: table }
---@type envy.dependency[]
DEPENDENCIES = {}

---Recipe products - paths relative to install_dir, or function returning same
---@type table<string, string>|fun(options: table): table<string, string>
PRODUCTS = {}

--------------------------------------------------------------------------------
-- Phase Functions/Values
--------------------------------------------------------------------------------

---@alias envy.fetch_source { source: string, sha256?: string, ref?: string }

---FETCH phase: declarative source specification or function
---@type envy.fetch_source|envy.fetch_source[]|fun(tmp_dir: string, options: table): string?, string?
FETCH = {}

---@alias envy.stage_opts { strip?: integer }

---STAGE phase: declarative options or function
---@type envy.stage_opts|fun(fetch_dir: string, stage_dir: string, tmp_dir: string, options: table)
STAGE = {}

---BUILD phase: shell script string or function
---@type string|fun(stage_dir: string, fetch_dir: string, tmp_dir: string, options: table)
BUILD = nil

---INSTALL phase: shell script string or function
---@type string|fun(install_dir: string, stage_dir: string, fetch_dir: string, tmp_dir: string, options: table)
INSTALL = nil

---CHECK phase: shell script string, function, or table with shell key
---Returns true if already satisfied (skip install), false to proceed
---@type string|fun(project_root: string, options: table): boolean|{ shell: string }
CHECK = nil

--------------------------------------------------------------------------------
-- Manifest Globals
--------------------------------------------------------------------------------

---@alias envy.package_spec string|{ recipe: string, source?: string, options?: table, needed_by?: string, product?: string, weak?: boolean }

---Manifest packages array
---@type envy.package_spec[]
PACKAGES = {}

---Default shell configuration for all phases
---@type integer|envy.shell_config|fun(ctx: { asset: fun(identity: string): string }): integer|envy.shell_config
DEFAULT_SHELL = nil

--------------------------------------------------------------------------------
-- Built-in print override
--------------------------------------------------------------------------------

---Print values (routed through TUI)
---@param ... any Values to print
function print(...) end

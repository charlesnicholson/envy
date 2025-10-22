# Envy Commands

Envy is a multi-tool CLI following Git's subcommand pattern. Each subcommand targets a distinct workflow; global flags apply across all commands.

## Global Flags

**`-v`, `--verbose`** — Enable structured logging; adds timestamps and severity tags.
**`-h`, `--help`, `help`** — Print top-level help summarizing available subcommands. Subcommands also support `--help` for detailed usage.

## Subcommands

### Meta

**`envy version`** — Print envy version and third-party component versions.

### Utilities

**`envy fetch <url> [destination]`** — Download file from any supported transport (HTTP/HTTPS, FTP/FTPS, SMB, Git, SSH, S3). Destination defaults to current directory with URL's filename. Verifies TLS, supports authentication (SSH keys, AWS credentials). Displays progress, optionally prints SHA256 on completion.

**`envy extract <archive> [destination]`** — Extract archive to specified location (defaults to current directory). Supports all libarchive formats: tar, tar.gz, tar.xz, tar.bz2, tar.zst, zip, 7z, rar, iso. Preserves permissions, timestamps, symlinks. Reports file count on completion.

**`envy compress <path> [output]`** — Create archive from file or directory. Format auto-detected from output extension (.tar.gz, .tgz, .tar.xz, .tar.bz2, .tar.zst, .tar, .zip). Defaults to `<basename>.tar.gz` if output not specified.

**`envy hash <path...> [--algorithm=...]`** — Compute and print cryptographic hashes for files and directories. Defaults to SHA256 and BLAKE3; supports sha256, blake3, sha1, md5. Recursively hashes all files in directories. Outputs in format `HASH  filename`.

**`envy hash-verify <file> <expected-hash> [--algorithm=...]`** — Verify file matches expected hash. Algorithm flag required (sha256, blake3, sha1, md5). Exits 0 if match, non-zero otherwise. Useful in scripts/CI.

**`envy lua [script]`** — Execute Lua script with envy's embedded runtime. If no script provided, opens interactive REPL. Exposes envy verbs (`fetch`, `extract`, `hash`) to Lua environment.

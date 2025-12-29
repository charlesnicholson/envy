# License Embedding for `envy version --licenses`

## Summary

`envy version --licenses` prints all third-party licenses (plus envy's own 0BSD/Unlicense). Licenses are auto-discovered from `out/cache/*-src/` at configure time, compressed at build time with gzip, embedded into the binary, and decompressed at runtime using zlib.

## Design

### Configure Time
`find_licenses.py` searches each dependency source directory for common license file names (LICENSE, COPYING, LICENSE.txt, etc.), writes a manifest of discovered paths.

### Build Time
`compress_licenses.py` reads the manifest, concatenates licenses with component headers, gzip-compresses the result. CMake `add_custom_command` with DEPENDS on all license files ensures rebuilds on changes.

### Runtime
zlib `inflate()` decompresses the embedded blob; output goes to stdout for piping.

## Output Format

```
================================================================================
envy
================================================================================

<license text>

================================================================================
BLAKE3
================================================================================

<license text>
...
```

## Files

| File | Purpose |
|------|---------|
| `LICENSE` | Envy's dual 0BSD/Unlicense |
| `cmake/scripts/find_licenses.py` | Discover license files in source dirs |
| `cmake/scripts/compress_licenses.py` | Concatenate and gzip compress |
| `cmake/FindLicenses.cmake` | CMake integration |
| `src/cmds/cmd_version.cpp` | Decompression and display |

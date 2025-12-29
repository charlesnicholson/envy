#!/usr/bin/env python3
"""Discover license files in source directories.

Usage: find_licenses.py <output-manifest> <name1>=<path1> [<name2>=<path2> ...]

For each name=path pair:
  - If path is a file, use it directly as the license
  - If path is a directory, search for common license file names

Writes manifest to output file: one "name|license_path" per line.
Exits non-zero if any entry has no discoverable license.
"""

import argparse
import sys
from pathlib import Path

# Common license file names, in priority order
LICENSE_NAMES = [
    "LICENSE",
    "LICENSE.txt",
    "LICENSE.md",
    "COPYING",
    "LICENCE",
    "license",
    "license.txt",
    "copying",
    "License.txt",
    "License",
    # Non-standard names used by some projects
    "LICENSE_A2",  # BLAKE3 Apache-2.0
    "LICENSE_MIT",  # Some projects use this
    "LICENSE-MIT",
    "LICENSE-APACHE",
    "COPYING.txt",
]

# Some projects put licenses in subdirectories
LICENSE_SUBDIRS = ["", "doc", "docs", "legal"]

# Fallback files to check if no standard license found (e.g., readme containing license)
LICENSE_FALLBACKS = [
    ("doc/readme.html", "Lua"),  # Lua embeds license in readme.html
]


def find_license_file(directory: Path, component_name: str = "") -> Path | None:
    """Search for a license file in the given directory."""
    # First, try standard license file names
    for subdir in LICENSE_SUBDIRS:
        search_dir = directory / subdir if subdir else directory
        if not search_dir.is_dir():
            continue
        for name in LICENSE_NAMES:
            candidate = search_dir / name
            if candidate.is_file():
                return candidate

    # Try fallback files for specific components
    for fallback_path, fallback_component in LICENSE_FALLBACKS:
        if component_name and fallback_component.lower() in component_name.lower():
            candidate = directory / fallback_path
            if candidate.is_file():
                return candidate

    return None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", help="Output manifest file path")
    parser.add_argument(
        "entries", nargs="+", metavar="NAME=PATH", help="Component name and source path"
    )
    args = parser.parse_args()

    manifest_lines = []
    errors = []

    for entry in args.entries:
        if "=" not in entry:
            errors.append(f"Invalid format (expected NAME=PATH): {entry}")
            continue

        name, path_str = entry.split("=", 1)
        path = Path(path_str)

        if path.is_file():
            # Direct file reference (e.g., envy's LICENSE)
            license_path = path
        elif path.is_dir():
            # Search for license in directory
            license_path = find_license_file(path, name)
            if license_path is None:
                errors.append(f"No license file found for '{name}' in {path}")
                continue
        else:
            errors.append(f"Path does not exist for '{name}': {path}")
            continue

        manifest_lines.append(f"{name}|{license_path}")

    if errors:
        for err in errors:
            print(f"ERROR: {err}", file=sys.stderr)
        return 1

    # Write manifest
    output_path = Path(args.output)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text("\n".join(manifest_lines) + "\n", encoding="utf-8")

    return 0


if __name__ == "__main__":
    sys.exit(main())

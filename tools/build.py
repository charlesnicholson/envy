#!/usr/bin/env python3
"""Build script for envy project."""

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description="Build envy project")
    parser.add_argument("--no-envy", action="store_true",
                        help="Skip building main envy executable")
    parser.add_argument("--no-functional-tester", action="store_true",
                        help="Skip building functional tester")
    parser.add_argument("--sanitizer", choices=["none", "asan", "ubsan", "tsan"],
                        default="none", help="Sanitizer to use (default: none)")
    args = parser.parse_args()

    # Check for cl.exe on Windows
    if sys.platform == "win32":
        if shutil.which("cl") is None:
            print("cl.exe not found on PATH. Please launch from a VS Developer Command Prompt or run vcvars64.bat first.")
            return 1

    script_dir = Path(__file__).parent.resolve()
    root_dir = script_dir.parent

    cmake_bin = os.environ.get("CMAKE", "cmake")

    cache_dir = root_dir / "out" / "cache"
    build_dir = root_dir / "out" / "build"
    cache_file = build_dir / "CMakeCache.txt"
    state_file = build_dir / ".envy-build-state"

    # Build current state string for comparison
    current_state = f"envy={not args.no_envy},functional={not args.no_functional_tester},sanitizer={args.sanitizer}"

    # Ensure cache directory exists
    cache_dir.mkdir(parents=True, exist_ok=True)

    # Check if we need to configure
    need_configure = True
    if cache_file.exists() and state_file.exists():
        cached_state = state_file.read_text().strip()
        if cached_state == current_state:
            need_configure = False

    if need_configure:
        cmake_args = [
            cmake_bin, "--preset", "release-lto-on",
            f"-DENVY_BUILD_ENVY={'OFF' if args.no_envy else 'ON'}",
            f"-DENVY_BUILD_FUNCTIONAL_TESTER={'OFF' if args.no_functional_tester else 'ON'}",
            f"-DENVY_SANITIZER={args.sanitizer.upper()}",
            "--log-level=STATUS"
        ]
        result = subprocess.run(cmake_args, cwd=root_dir)
        if result.returncode != 0:
            return 1

        build_dir.mkdir(parents=True, exist_ok=True)
        state_file.write_text(current_state + "\n")

    result = subprocess.run(
        [cmake_bin, "--build", "--preset", "release-lto-on"],
        cwd=root_dir
    )
    if result.returncode != 0:
        print("\nBuild failed. See messages above for details.")
        return 1

    print("Build completed successfully.")
    return 0


if __name__ == "__main__":
    sys.exit(main())

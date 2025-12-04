#!/usr/bin/env python3
"""Build script for envy project."""

import os
import shutil
import subprocess
import sys
from pathlib import Path


def main():
    # Check for cl.exe on Windows
    if sys.platform == "win32":
        if shutil.which("cl") is None:
            print("cl.exe not found on PATH. Please launch from a VS Developer Command Prompt or run vcvars64.bat first.")
            return 1

    script_dir = Path(__file__).parent.resolve()
    root_dir = script_dir.parent

    cmake_bin = os.environ.get("CMAKE", "cmake")

    # Parse arguments: "asan" enables AddressSanitizer
    preset = "release-lto-on"
    asan_state = "OFF"
    asan_flag = "-DENVY_ENABLE_ASAN=OFF"
    if len(sys.argv) > 1 and sys.argv[1].lower() == "asan":
        preset = "release-asan-lto-on"
        asan_state = "ON"
        asan_flag = "-DENVY_ENABLE_ASAN=ON"

    cache_dir = root_dir / "out" / "cache"
    build_dir = root_dir / "out" / "build"
    cache_file = build_dir / "CMakeCache.txt"
    preset_file = build_dir / ".envy-preset"
    asan_state_file = build_dir / ".envy-asan-state"

    # Ensure cache directory exists
    cache_dir.mkdir(parents=True, exist_ok=True)

    # Check if we need to configure
    need_configure = True
    cached_preset = None
    cached_asan_state = None

    if cache_file.exists():
        if preset_file.exists():
            cached_preset = preset_file.read_text().strip()
        if asan_state_file.exists():
            cached_asan_state = asan_state_file.read_text().strip()

        if cached_preset == preset and cached_asan_state == asan_state:
            need_configure = False

    if need_configure:
        result = subprocess.run(
            [cmake_bin, "--preset", preset, asan_flag, "--log-level=STATUS"],
            cwd=root_dir
        )
        if result.returncode != 0:
            return 1

        build_dir.mkdir(parents=True, exist_ok=True)
        preset_file.write_text(preset + "\n")
        asan_state_file.write_text(asan_state + "\n")

    result = subprocess.run(
        [cmake_bin, "--build", "--preset", preset],
        cwd=root_dir
    )
    if result.returncode != 0:
        print("\nBuild failed. See messages above for details.")
        return 1

    print("Build completed successfully.")
    return 0


if __name__ == "__main__":
    sys.exit(main())

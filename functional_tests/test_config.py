"""Shared configuration for functional tests."""

import os
import shlex
import subprocess
import sys
import time
from pathlib import Path

# Subprocess text mode kwargs - use UTF-8 to avoid cp1252 decode errors on Windows
SUBPROCESS_TEXT_MODE = {"text": True, "encoding": "utf-8", "errors": "replace"}


def _is_envy_cmd(cmd) -> bool:
    """Return True if cmd invokes the envy functional tester binary."""
    if not cmd:
        return False
    exe = str(cmd[0] if not isinstance(cmd, str) else cmd)
    return "envy_functional_tester" in exe


def _wrap_cmd(cmd):
    """Prepend ENVY_TEST_WRAPPER to a command list if set (envy commands only)."""
    wrapper = os.environ.get("ENVY_TEST_WRAPPER")
    if wrapper and _is_envy_cmd(cmd):
        return shlex.split(wrapper) + list(cmd)
    return cmd


def run(*args, **kwargs) -> subprocess.CompletedProcess[str]:
    """Wrapper for subprocess.run with UTF-8 encoding and optional command wrapping."""
    kwargs.setdefault("encoding", "utf-8")
    kwargs.setdefault("errors", "replace")
    if "text" not in kwargs and "encoding" in kwargs:
        kwargs["text"] = True
    args = list(args)
    if args and not kwargs.get("shell"):
        args[0] = _wrap_cmd(args[0])
    return subprocess.run(*args, **kwargs)


def is_pwsh_runtime_crash(result: subprocess.CompletedProcess) -> bool:
    """True if pwsh's .NET runtime crashed at startup rather than running the script.

    pwsh intermittently aborts at process startup on resource-constrained CI
    runners (observed on linux-arm64 under ASAN). Two CoreCLR signatures seen:
    - "Unhandled exception ... The given assembly name was invalid" (SIGABRT)
    - a bare "Stack overflow." (SIGABRT)
    Both fire before the script executes and are nondeterministic and unrelated
    to the behavior under test. A genuine script failure exits cleanly with the
    wrong output (not a managed-runtime abort), so these signatures are safe to
    retry without masking a real bug — a deterministic crash still fails after
    the retries are exhausted.
    """
    if result.returncode == 0:
        return False
    stderr = result.stderr or ""
    if "Unhandled exception" in stderr and (
        "FileLoadException" in stderr or "assembly" in stderr
    ):
        return True
    return "Stack overflow." in stderr


def run_pwsh(cmd, retries: int = 3, delay: float = 0.5, **kwargs):
    """Run a pwsh command, retrying .NET-runtime startup crashes.

    See is_pwsh_runtime_crash; genuine failures are returned as-is.
    """
    result = None
    for attempt in range(retries):
        result = run(cmd, **kwargs)
        if not is_pwsh_runtime_crash(result):
            return result
        sys.stderr.write(
            f"pwsh runtime crash (attempt {attempt + 1}/{retries}), retrying: "
            f"{(result.stderr or '').strip()[:120]}\n"
        )
        time.sleep(delay)
    return result


def popen(*args, **kwargs) -> subprocess.Popen[str]:
    """Wrapper for subprocess.Popen with UTF-8 encoding and optional command wrapping."""
    kwargs.setdefault("encoding", "utf-8")
    kwargs.setdefault("errors", "replace")
    if "text" not in kwargs and "encoding" in kwargs:
        kwargs["text"] = True
    args = list(args)
    if args and not kwargs.get("shell"):
        args[0] = _wrap_cmd(args[0])
    return subprocess.Popen(*args, **kwargs)


# Required manifest header for all manifests (bin is mandatory)
MANIFEST_HEADER = '-- @envy bin "envy-bin"\n'

# Manifest header with deployment enabled (for sync tests)
MANIFEST_HEADER_DEPLOY = '-- @envy bin "envy-bin"\n-- @envy deploy "true"\n'


def make_manifest(packages_content: str, deploy: bool = False) -> str:
    """Create a manifest string with required headers.

    Args:
        packages_content: The PACKAGES table content (should start with 'PACKAGES = {')
        deploy: If True, include deploy directive for product script creation

    Returns:
        Complete manifest string with required bin directive.
    """
    header = MANIFEST_HEADER_DEPLOY if deploy else MANIFEST_HEADER
    return header + packages_content


def parse_export_line(line):
    """Parse an export output line '<hash>  <path>' -> (hash, Path)."""
    parts = line.strip().split("  ", 1)
    if len(parts) != 2:
        raise ValueError(f"Expected '<hash>  <path>', got: {line}")
    return parts[0], Path(parts[1])


_cached_executable: Path | None = None


def get_envy_executable() -> Path:
    """Get the path to the envy functional test executable."""
    global _cached_executable
    if _cached_executable is not None:
        return _cached_executable

    root = Path(__file__).parent.parent / "out" / "build"

    if sys.platform == "win32":
        exe = root / "envy_functional_tester.exe"
    else:
        exe = root / "envy_functional_tester"

    if not exe.exists():
        raise RuntimeError(
            f"Functional tester not found at {exe}. "
            "Build with: ./build.sh (or ./build.bat on Windows)"
        )

    if sys.platform != "win32" and not os.access(exe, os.X_OK):
        raise RuntimeError(f"Functional tester at {exe} is not executable")

    _cached_executable = exe
    return exe


def get_test_env() -> dict[str, str]:
    """Get environment variables for running tests."""
    env = os.environ.copy()

    # Sanitizers not supported on Windows
    if sys.platform == "win32":
        return env

    root = Path(__file__).parent.parent

    # Point sanitizers to suppression files
    tsan_supp = root / "tsan.supp"
    if tsan_supp.exists():
        env.setdefault("TSAN_OPTIONS", f"suppressions={tsan_supp}")

    asan_supp = root / "asan.supp"
    if asan_supp.exists():
        env.setdefault("ASAN_OPTIONS", f"suppressions={asan_supp}")

    return env

"""Functional tests for SETUP pairs (named host-side CHECK/INSTALL work).

Covers:
- Manifest `setup` selection of pairs on cache-managed packages
- pkg_dir argument (payload path for cache-managed, nil for user-managed)
- Selection defaults (user-managed: all pairs; cache-managed: none)
- Narrowing and empty selections
- Unknown pair name errors
- Per-pair PLATFORMS filtering
- Idempotency: CHECK gates re-runs; cache-hit runs still evaluate pairs
- Pair failure propagation to dependents
- Dependents wait for a dependency's setup phase
- Concurrent envy processes: double-check lock runs INSTALL exactly once
"""

import hashlib
import io
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path
from typing import List, Optional

from . import test_config
from .test_config import make_manifest

TEST_ARCHIVE_FILES = {
    "root/payload.txt": "payload-content\n",
}


def create_test_archive(output_path: Path) -> str:
    """Create test.tar.gz archive and return its SHA256 hash."""
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w:gz") as tar:
        for name, content in TEST_ARCHIVE_FILES.items():
            data = content.encode("utf-8")
            info = tarfile.TarInfo(name=name)
            info.size = len(data)
            tar.addfile(info, io.BytesIO(data))
    archive_data = buf.getvalue()
    output_path.write_bytes(archive_data)
    return hashlib.sha256(archive_data).hexdigest()


# Cache-managed package with two optional pairs. The `extras` pair records the
# pkg_dir it received; `udev` appends to a log to observe (non-)execution.
SPEC_CACHED_WITH_SETUP = """IDENTITY = "local.cached_setup@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}",
}}

STAGE = {{strip = 1}}

SETUP = {{
  extras = {{
    CHECK = function(pkg_dir, options)
      local f = io.open("extras_marker.txt", "r")
      if f then f:close(); return true end
      return false
    end,
    INSTALL = function(pkg_dir, options)
      if type(pkg_dir) ~= "string" then error("pkg_dir must be a string for cache-managed") end
      local payload = io.open(pkg_dir .. "payload.txt", "r")
      if not payload then error("pkg_dir does not contain payload.txt: " .. pkg_dir) end
      local content = payload:read("*all")
      payload:close()
      local f = io.open("extras_marker.txt", "w")
      f:write(content)
      f:close()
    end,
  }},
  udev = {{
    CHECK = function(pkg_dir, options)
      return false
    end,
    INSTALL = function(pkg_dir, options)
      local f = io.open("udev_log.txt", "a")
      f:write("udev-install\\n")
      f:close()
    end,
  }},
}}
"""

# User-managed package with two pairs, each appending its name to a log.
SPEC_UM_TWO_PAIRS = """IDENTITY = "local.um_two_pairs@v1"
USER_MANAGED = true

SETUP = {{
  alpha = {{
    CHECK = function(pkg_dir, options)
      if pkg_dir ~= nil then error("pkg_dir must be nil for user-managed") end
      return false
    end,
    INSTALL = function(pkg_dir, options)
      local f = io.open("pair_log.txt", "a")
      f:write("alpha\\n")
      f:close()
    end,
  }},
  beta = {{
    CHECK = function(pkg_dir, options)
      return false
    end,
    INSTALL = function(pkg_dir, options)
      local f = io.open("pair_log.txt", "a")
      f:write("beta\\n")
      f:close()
    end,
  }},
}}
"""


class TestSetupPairs(unittest.TestCase):
    """Manifest-driven SETUP pair behavior."""

    # Concurrency test runs two envy processes with a sleeping install.
    envy_watchdog_timeout = 60

    def setUp(self):
        self.cache_root = Path(tempfile.mkdtemp(prefix="envy-setup-test-"))
        self.test_dir = Path(tempfile.mkdtemp(prefix="envy-setup-manifest-"))
        self.specs_dir = Path(tempfile.mkdtemp(prefix="envy-setup-specs-"))
        self.envy = test_config.get_envy_executable()
        self.project_root = Path(__file__).parent.parent

        self.archive_path = self.specs_dir / "test.tar.gz"
        self.archive_hash = create_test_archive(self.archive_path)

    def tearDown(self):
        shutil.rmtree(self.cache_root, ignore_errors=True)
        shutil.rmtree(self.test_dir, ignore_errors=True)
        shutil.rmtree(self.specs_dir, ignore_errors=True)

    def write_spec(self, name: str, content: str) -> str:
        """Write spec to temp dir with placeholder substitution, return Lua path."""
        spec_content = content.format(
            ARCHIVE_PATH=self.archive_path.as_posix(),
            ARCHIVE_HASH=self.archive_hash,
        )
        path = self.specs_dir / f"{name}.lua"
        path.write_text(spec_content, encoding="utf-8")
        return path.as_posix()

    def create_manifest(self, content: str, manifest_dir: Optional[Path] = None) -> Path:
        """Create manifest file with given content."""
        manifest_dir = manifest_dir or self.test_dir
        manifest_path = manifest_dir / "envy.lua"
        manifest_path.write_text(make_manifest(content), encoding="utf-8")
        return manifest_path

    def run_install(
        self,
        manifest: Path,
        should_fail: bool = False,
        trace: bool = False,
    ):
        cmd = [str(self.envy), "--cache-root", str(self.cache_root)]
        if trace:
            cmd.append("--trace")
        cmd.extend(["install", "--manifest", str(manifest)])

        # cwd = manifest dir: pair verbs use plain Lua io.* which resolves
        # against the process cwd, and pair shell cwd is project_root anyway.
        result = test_config.run(
            cmd, cwd=self.test_dir, capture_output=True, text=True
        )
        if should_fail:
            self.assertNotEqual(
                result.returncode,
                0,
                f"Expected failure.\nstdout: {result.stdout}\nstderr: {result.stderr}",
            )
        else:
            self.assertEqual(
                result.returncode,
                0,
                f"stdout: {result.stdout}\nstderr: {result.stderr}",
            )
        return result

    # =========================================================================
    # Cache-managed selection
    # =========================================================================

    def test_unselected_pairs_do_not_run(self):
        """Cache-managed package installs payload; unselected pairs never run."""
        spec_path = self.write_spec("cached_setup", SPEC_CACHED_WITH_SETUP)
        manifest = self.create_manifest(
            f'PACKAGES = {{ {{ spec = "local.cached_setup@v1", source = "{spec_path}" }} }}'
        )
        self.run_install(manifest)

        self.assertFalse((self.test_dir / "extras_marker.txt").exists())
        self.assertFalse((self.test_dir / "udev_log.txt").exists())

        pkg_dir = self.cache_root / "packages" / "local.cached_setup@v1"
        self.assertTrue(any(pkg_dir.glob("*/pkg")), "payload should be cached")

    def test_selected_pair_runs_with_pkg_dir(self):
        """Selected pair runs after payload install and receives the payload path."""
        spec_path = self.write_spec("cached_setup", SPEC_CACHED_WITH_SETUP)
        manifest = self.create_manifest(
            f'PACKAGES = {{ {{ spec = "local.cached_setup@v1", source = "{spec_path}", '
            f'setup = {{ "extras" }} }} }}'
        )
        self.run_install(manifest)

        marker = self.test_dir / "extras_marker.txt"
        self.assertTrue(marker.exists(), "selected pair INSTALL should have run")
        self.assertEqual(marker.read_text(), "payload-content\n")
        self.assertFalse(
            (self.test_dir / "udev_log.txt").exists(), "unselected pair must not run"
        )

    def test_selected_pair_runs_on_cache_hit(self):
        """Pairs are evaluated every run, even when the payload is a cache hit."""
        spec_path = self.write_spec("cached_setup", SPEC_CACHED_WITH_SETUP)
        no_setup = self.create_manifest(
            f'PACKAGES = {{ {{ spec = "local.cached_setup@v1", source = "{spec_path}" }} }}'
        )
        self.run_install(no_setup)  # populate payload cache, no pairs

        with_setup = self.create_manifest(
            f'PACKAGES = {{ {{ spec = "local.cached_setup@v1", source = "{spec_path}", '
            f'setup = {{ "extras" }} }} }}'
        )
        result = self.run_install(with_setup, trace=True)

        self.assertIn("CACHE HIT", result.stderr)
        self.assertTrue(
            (self.test_dir / "extras_marker.txt").exists(),
            "pair must run even on payload cache hit",
        )

    def test_pair_idempotent_across_runs(self):
        """Second run with satisfied CHECK does not re-run INSTALL."""
        spec_path = self.write_spec("cached_setup", SPEC_CACHED_WITH_SETUP)
        manifest = self.create_manifest(
            f'PACKAGES = {{ {{ spec = "local.cached_setup@v1", source = "{spec_path}", '
            f'setup = {{ "extras", "udev" }} }} }}'
        )
        self.run_install(manifest)
        self.run_install(manifest)

        # extras: CHECK satisfied after first run -> installed once
        self.assertTrue((self.test_dir / "extras_marker.txt").exists())
        # udev: CHECK always false -> runs every time
        udev_log = self.test_dir / "udev_log.txt"
        self.assertEqual(udev_log.read_text().count("udev-install"), 2)

    def test_unknown_setup_name_fails(self):
        """Selecting a pair name the spec does not define is a hard error."""
        spec_path = self.write_spec("cached_setup", SPEC_CACHED_WITH_SETUP)
        manifest = self.create_manifest(
            f'PACKAGES = {{ {{ spec = "local.cached_setup@v1", source = "{spec_path}", '
            f'setup = {{ "nonexistent" }} }} }}'
        )
        result = self.run_install(manifest, should_fail=True)
        self.assertIn("Unknown setup pair 'nonexistent'", result.stderr)

    def test_selection_on_spec_without_setup_fails(self):
        """Selecting pairs on a spec that defines no SETUP is a hard error."""
        spec = """IDENTITY = "local.no_setup@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}",
}}
"""
        spec_path = self.write_spec("no_setup", spec)
        manifest = self.create_manifest(
            f'PACKAGES = {{ {{ spec = "local.no_setup@v1", source = "{spec_path}", '
            f'setup = {{ "anything" }} }} }}'
        )
        result = self.run_install(manifest, should_fail=True)
        self.assertIn("no SETUP pairs", result.stderr)

    def test_platform_filtered_pair_skipped(self):
        """A pair whose PLATFORMS excludes the host is silently skipped."""
        other_os = "windows" if sys.platform != "win32" else "linux"
        spec = f"""IDENTITY = "local.plat_pair@v1"

FETCH = {{{{
  source = "{{ARCHIVE_PATH}}",
  sha256 = "{{ARCHIVE_HASH}}",
}}}}

SETUP = {{{{
  other_os_only = {{{{
    PLATFORMS = {{{{ "{other_os}" }}}},
    CHECK = function(pkg_dir, options) return false end,
    INSTALL = function(pkg_dir, options)
      error("must not run on this platform")
    end,
  }}}},
}}}}
"""
        spec_path = self.write_spec("plat_pair", spec)
        manifest = self.create_manifest(
            f'PACKAGES = {{ {{ spec = "local.plat_pair@v1", source = "{spec_path}", '
            f'setup = {{ "other_os_only" }} }} }}'
        )
        result = self.run_install(manifest, trace=True)
        self.assertIn("skipped (platform mismatch)", result.stderr)

    # =========================================================================
    # User-managed selection defaults
    # =========================================================================

    def test_user_managed_default_runs_all_pairs_sorted(self):
        """User-managed package with no `setup` field runs all pairs, sorted."""
        spec_path = self.write_spec("um_two_pairs", SPEC_UM_TWO_PAIRS)
        manifest = self.create_manifest(
            f'PACKAGES = {{ {{ spec = "local.um_two_pairs@v1", source = "{spec_path}" }} }}'
        )
        self.run_install(manifest)
        log = self.test_dir / "pair_log.txt"
        self.assertEqual(log.read_text().split(), ["alpha", "beta"])

    def test_user_managed_narrowed_selection(self):
        """Explicit `setup` list narrows a user-managed package's pairs."""
        spec_path = self.write_spec("um_two_pairs", SPEC_UM_TWO_PAIRS)
        manifest = self.create_manifest(
            f'PACKAGES = {{ {{ spec = "local.um_two_pairs@v1", source = "{spec_path}", '
            f'setup = {{ "beta" }} }} }}'
        )
        self.run_install(manifest)
        log = self.test_dir / "pair_log.txt"
        self.assertEqual(log.read_text().split(), ["beta"])

    def test_user_managed_empty_selection_runs_nothing(self):
        """Explicit empty `setup` list disables all pairs."""
        spec_path = self.write_spec("um_two_pairs", SPEC_UM_TWO_PAIRS)
        manifest = self.create_manifest(
            f'PACKAGES = {{ {{ spec = "local.um_two_pairs@v1", source = "{spec_path}", '
            f"setup = {{ }} }} }}"
        )
        self.run_install(manifest)
        self.assertFalse((self.test_dir / "pair_log.txt").exists())

    # =========================================================================
    # Failure propagation and dependency ordering
    # =========================================================================

    def test_pair_failure_fails_run_and_blocks_dependents(self):
        """A failing selected pair fails the run; dependents never install."""
        failing = """IDENTITY = "local.failing_pair@v1"
USER_MANAGED = true

SETUP = {{
  main = {{
    CHECK = function(pkg_dir, options) return false end,
    INSTALL = function(pkg_dir, options)
      error("intentional pair failure")
    end,
  }},
}}
"""
        dependent = f"""IDENTITY = "local.dependent@v1"
USER_MANAGED = true

DEPENDENCIES = {{{{
  {{{{ spec = "local.failing_pair@v1", source = "failing_pair.lua" }}}}
}}}}

SETUP = {{{{
  main = {{{{
    CHECK = function(pkg_dir, options) return false end,
    INSTALL = function(pkg_dir, options)
      local f = io.open("dependent_marker.txt", "w")
      f:write("ran")
      f:close()
    end,
  }}}},
}}}}
"""
        self.write_spec("failing_pair", failing)
        dep_path = self.write_spec("dependent", dependent)
        manifest = self.create_manifest(
            f'PACKAGES = {{ {{ spec = "local.dependent@v1", source = "{dep_path}" }} }}'
        )
        result = self.run_install(manifest, should_fail=True)
        self.assertIn("intentional pair failure", result.stderr)
        self.assertFalse(
            (self.test_dir / "dependent_marker.txt").exists(),
            "dependent must not run when dependency's pair fails",
        )

    def test_dependent_waits_for_dependency_setup(self):
        """A dependent's pairs run only after the dependency's pairs complete."""
        dep = """IDENTITY = "local.order_dep@v1"
USER_MANAGED = true

SETUP = {{
  main = {{
    CHECK = function(pkg_dir, options) return false end,
    INSTALL = function(pkg_dir, options)
      local f = io.open("order_log.txt", "a")
      f:write("dep\\n")
      f:close()
    end,
  }},
}}
"""
        top = f"""IDENTITY = "local.order_top@v1"
USER_MANAGED = true

DEPENDENCIES = {{{{
  {{{{ spec = "local.order_dep@v1", source = "order_dep.lua" }}}}
}}}}

SETUP = {{{{
  main = {{{{
    CHECK = function(pkg_dir, options) return false end,
    INSTALL = function(pkg_dir, options)
      local f = io.open("order_log.txt", "a")
      f:write("top\\n")
      f:close()
    end,
  }}}},
}}}}
"""
        self.write_spec("order_dep", dep)
        top_path = self.write_spec("order_top", top)
        manifest = self.create_manifest(
            f'PACKAGES = {{ {{ spec = "local.order_top@v1", source = "{top_path}" }} }}'
        )
        self.run_install(manifest)
        log = self.test_dir / "order_log.txt"
        self.assertEqual(log.read_text().split(), ["dep", "top"])

    # =========================================================================
    # Concurrency: double-check lock
    # =========================================================================

    def test_concurrent_processes_install_pair_once(self):
        """Two concurrent envy runs: the pair lock + re-check runs INSTALL once."""
        sleep_cmd = (
            "Start-Sleep -Seconds 1" if sys.platform == "win32" else "sleep 1"
        )
        spec = f"""IDENTITY = "local.concurrent_pair@v1"
USER_MANAGED = true

SETUP = {{{{
  main = {{{{
    CHECK = function(pkg_dir, options)
      local f = io.open("concurrent_marker.txt", "r")
      if f then f:close(); return true end
      return false
    end,
    INSTALL = function(pkg_dir, options)
      envy.run("{sleep_cmd}", {{{{ quiet = true }}}})
      local f = io.open("concurrent_log.txt", "a")
      f:write("install\\n")
      f:close()
      local m = io.open("concurrent_marker.txt", "w")
      m:write("done")
      m:close()
    end,
  }}}},
}}}}
"""
        spec_path = self.write_spec("concurrent_pair", spec)
        manifest = self.create_manifest(
            f'PACKAGES = {{ {{ spec = "local.concurrent_pair@v1", source = "{spec_path}" }} }}'
        )

        cmd = [
            str(self.envy),
            "--cache-root",
            str(self.cache_root),
            "install",
            "--manifest",
            str(manifest),
        ]
        procs = [
            subprocess.Popen(
                cmd,
                cwd=self.test_dir,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
            for _ in range(2)
        ]
        results = [p.communicate(timeout=45) for p in procs]
        for p, (out, err) in zip(procs, results):
            self.assertEqual(p.returncode, 0, f"stdout: {out}\nstderr: {err}")

        log = self.test_dir / "concurrent_log.txt"
        self.assertTrue(log.exists())
        self.assertEqual(
            log.read_text().count("install"),
            1,
            "double-check lock must ensure exactly one INSTALL",
        )


if __name__ == "__main__":
    unittest.main()

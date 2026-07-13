"""Functional tests for SETUP pairs (named host-side CHECK/INSTALL work).

Covers:
- Explicit-only selection: pairs run only when a manifest or dependency entry
  selects them; no selection (any package type) runs nothing
- pkg_dir argument (payload path for cache-managed, nil for user-managed)
- Unknown pair name errors
- Per-pair PLATFORMS filtering
- DEPENDS: sibling sequencing, transitive auto-selection, diamond closure
- Parallelism: unrelated selected pairs overlap in time
- Idempotency: CHECK gates re-runs; cache-hit runs still evaluate pairs
- Pair failure propagation: dependent pairs blocked, unrelated pairs complete
- Dependency entries selecting pairs (`setup` on DEPENDENCIES entries)
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
    # Explicit-only selection (no defaults)
    # =========================================================================

    def test_user_managed_without_selection_runs_nothing(self):
        """User-managed package with no `setup` field runs no pairs (explicit-only)."""
        spec_path = self.write_spec("um_two_pairs", SPEC_UM_TWO_PAIRS)
        manifest = self.create_manifest(
            f'PACKAGES = {{ {{ spec = "local.um_two_pairs@v1", source = "{spec_path}" }} }}'
        )
        self.run_install(manifest)
        self.assertFalse((self.test_dir / "pair_log.txt").exists())

    def test_user_managed_full_selection_runs_all_pairs(self):
        """Selecting every pair runs every pair (order unspecified: parallel)."""
        spec_path = self.write_spec("um_two_pairs", SPEC_UM_TWO_PAIRS)
        manifest = self.create_manifest(
            f'PACKAGES = {{ {{ spec = "local.um_two_pairs@v1", source = "{spec_path}", '
            f'setup = {{ "alpha", "beta" }} }} }}'
        )
        self.run_install(manifest)
        log = self.test_dir / "pair_log.txt"
        self.assertEqual(sorted(log.read_text().split()), ["alpha", "beta"])

    def test_user_managed_narrowed_selection(self):
        """Explicit `setup` list selects only the named pairs."""
        spec_path = self.write_spec("um_two_pairs", SPEC_UM_TWO_PAIRS)
        manifest = self.create_manifest(
            f'PACKAGES = {{ {{ spec = "local.um_two_pairs@v1", source = "{spec_path}", '
            f'setup = {{ "beta" }} }} }}'
        )
        self.run_install(manifest)
        log = self.test_dir / "pair_log.txt"
        self.assertEqual(log.read_text().split(), ["beta"])

    def test_user_managed_empty_selection_runs_nothing(self):
        """Explicit empty `setup` list selects nothing (same as absent)."""
        spec_path = self.write_spec("um_two_pairs", SPEC_UM_TWO_PAIRS)
        manifest = self.create_manifest(
            f'PACKAGES = {{ {{ spec = "local.um_two_pairs@v1", source = "{spec_path}", '
            f"setup = {{ }} }} }}"
        )
        self.run_install(manifest)
        self.assertFalse((self.test_dir / "pair_log.txt").exists())

    # =========================================================================
    # DEPENDS: sequencing, auto-selection, parallelism
    # =========================================================================

    SPEC_UM_DEPENDS_CHAIN = """IDENTITY = "local.um_chain@v1"
USER_MANAGED = true

local function log_pair(name)
  local f = io.open("chain_log.txt", "a")
  f:write(name .. "\\n")
  f:close()
end

SETUP = {{
  a = {{
    CHECK = function() return false end,
    INSTALL = function() log_pair("a") end,
  }},
  b = {{
    DEPENDS = {{ "a" }},
    CHECK = function() return false end,
    INSTALL = function() log_pair("b") end,
  }},
  c = {{
    DEPENDS = {{ "b" }},
    CHECK = function() return false end,
    INSTALL = function() log_pair("c") end,
  }},
}}
"""

    def test_depends_chain_runs_in_order(self):
        """b DEPENDS a, c DEPENDS b: install order is strictly a, b, c."""
        spec_path = self.write_spec("um_chain", self.SPEC_UM_DEPENDS_CHAIN)
        manifest = self.create_manifest(
            f'PACKAGES = {{ {{ spec = "local.um_chain@v1", source = "{spec_path}", '
            f'setup = {{ "a", "b", "c" }} }} }}'
        )
        self.run_install(manifest)
        log = self.test_dir / "chain_log.txt"
        self.assertEqual(log.read_text().split(), ["a", "b", "c"])

    def test_depends_auto_selects_prerequisites(self):
        """Selecting only the chain tail transitively selects and runs its DEPENDS."""
        spec_path = self.write_spec("um_chain", self.SPEC_UM_DEPENDS_CHAIN)
        manifest = self.create_manifest(
            f'PACKAGES = {{ {{ spec = "local.um_chain@v1", source = "{spec_path}", '
            f'setup = {{ "c" }} }} }}'
        )
        self.run_install(manifest)
        log = self.test_dir / "chain_log.txt"
        self.assertEqual(log.read_text().split(), ["a", "b", "c"])

    def test_depends_diamond_runs_each_pair_once_in_order(self):
        """Diamond (d -> b,c -> a): a first, d last, b/c between, each exactly once."""
        spec = """IDENTITY = "local.um_diamond@v1"
USER_MANAGED = true

local function log_pair(name)
  local f = io.open("diamond_log.txt", "a")
  f:write(name .. "\\n")
  f:close()
end

SETUP = {{
  a = {{ CHECK = function() return false end, INSTALL = function() log_pair("a") end }},
  b = {{
    DEPENDS = {{ "a" }},
    CHECK = function() return false end,
    INSTALL = function() log_pair("b") end,
  }},
  c = {{
    DEPENDS = {{ "a" }},
    CHECK = function() return false end,
    INSTALL = function() log_pair("c") end,
  }},
  d = {{
    DEPENDS = {{ "b", "c" }},
    CHECK = function() return false end,
    INSTALL = function() log_pair("d") end,
  }},
}}
"""
        spec_path = self.write_spec("um_diamond", spec)
        manifest = self.create_manifest(
            f'PACKAGES = {{ {{ spec = "local.um_diamond@v1", source = "{spec_path}", '
            f'setup = {{ "d" }} }} }}'
        )
        self.run_install(manifest)
        entries = (self.test_dir / "diamond_log.txt").read_text().split()
        self.assertEqual(sorted(entries), ["a", "b", "c", "d"])
        self.assertEqual(entries[0], "a")
        self.assertEqual(entries[-1], "d")

    def test_unrelated_pairs_run_in_parallel(self):
        """Two independent pairs with 2s shell installs overlap in wall-clock time.

        INSTALL uses the shell-string form: string scripts run outside the
        package's Lua lock, so unrelated pair nodes execute concurrently.
        """
        # One stamp file per pair: concurrent appends to a shared file are not
        # safe under PowerShell's Add-Content.
        if sys.platform == "win32":
            stamp = (
                'Add-Content stamps_{name}.txt "{edge} '
                '$([DateTimeOffset]::Now.ToUnixTimeSeconds())"'
            )
            sleep = "Start-Sleep -Seconds 2"
        else:
            stamp = 'echo "{edge} $(date +%s)" >> stamps_{name}.txt'
            sleep = "sleep 2"

        def install_script(name: str) -> str:
            return "; ".join(
                [stamp.format(name=name, edge="start"), sleep,
                 stamp.format(name=name, edge="end")]
            )

        spec = f"""IDENTITY = "local.um_parallel@v1"
USER_MANAGED = true

SETUP = {{{{
  left = {{{{
    CHECK = function() return false end,
    INSTALL = [[{install_script("left")}]],
  }}}},
  right = {{{{
    CHECK = function() return false end,
    INSTALL = [[{install_script("right")}]],
  }}}},
}}}}
"""
        spec_path = self.write_spec("um_parallel", spec)
        manifest = self.create_manifest(
            f'PACKAGES = {{ {{ spec = "local.um_parallel@v1", source = "{spec_path}", '
            f'setup = {{ "left", "right" }} }} }}'
        )
        self.run_install(manifest)

        events = {}
        for name in ("left", "right"):
            for line in (self.test_dir / f"stamps_{name}.txt").read_text().splitlines():
                edge, when = line.split()
                events[(name, edge)] = int(when)

        # Overlap: each pair starts before the other finishes. Serial execution
        # of two 2s installs cannot satisfy both inequalities.
        self.assertLess(events[("left", "start")], events[("right", "end")])
        self.assertLess(events[("right", "start")], events[("left", "end")])

    def test_platform_filtered_depends_still_satisfies_dependents(self):
        """A DEPENDS target filtered out by PLATFORMS skips but unblocks dependents."""
        other_os = "windows" if sys.platform != "win32" else "linux"
        spec = f"""IDENTITY = "local.um_plat_dep@v1"
USER_MANAGED = true

SETUP = {{{{
  gated = {{{{
    PLATFORMS = {{{{ "{other_os}" }}}},
    CHECK = function() return false end,
    INSTALL = function() error("must not run on this platform") end,
  }}}},
  dependent = {{{{
    DEPENDS = {{{{ "gated" }}}},
    CHECK = function() return false end,
    INSTALL = function()
      local f = io.open("plat_dep_marker.txt", "w")
      f:write("ran")
      f:close()
    end,
  }}}},
}}}}
"""
        spec_path = self.write_spec("um_plat_dep", spec)
        manifest = self.create_manifest(
            f'PACKAGES = {{ {{ spec = "local.um_plat_dep@v1", source = "{spec_path}", '
            f'setup = {{ "dependent" }} }} }}'
        )
        self.run_install(manifest)
        self.assertTrue((self.test_dir / "plat_dep_marker.txt").exists())

    def test_pair_failure_blocks_dependent_pair_but_not_unrelated(self):
        """Failing pair: its dependent pair never runs; an unrelated pair completes."""
        spec = """IDENTITY = "local.um_fail_iso@v1"
USER_MANAGED = true

local function mark(name)
  local f = io.open("iso_" .. name .. ".txt", "w")
  f:write("ran")
  f:close()
end

SETUP = {{
  bad = {{
    CHECK = function() return false end,
    INSTALL = function() error("intentional bad-pair failure") end,
  }},
  blocked = {{
    DEPENDS = {{ "bad" }},
    CHECK = function() return false end,
    INSTALL = function() mark("blocked") end,
  }},
  unrelated = {{
    CHECK = function() return false end,
    INSTALL = function() mark("unrelated") end,
  }},
}}
"""
        spec_path = self.write_spec("um_fail_iso", spec)
        manifest = self.create_manifest(
            f'PACKAGES = {{ {{ spec = "local.um_fail_iso@v1", source = "{spec_path}", '
            f'setup = {{ "blocked", "unrelated" }} }} }}'
        )
        result = self.run_install(manifest, should_fail=True)
        self.assertIn("intentional bad-pair failure", result.stderr)
        self.assertFalse((self.test_dir / "iso_blocked.txt").exists())
        self.assertTrue((self.test_dir / "iso_unrelated.txt").exists())

    def test_dependency_entry_selects_pairs(self):
        """A DEPENDENCIES entry's `setup` list selects pairs on the dependency."""
        dep = """IDENTITY = "local.dep_sel_target@v1"
USER_MANAGED = true

SETUP = {{
  main = {{
    CHECK = function() return false end,
    INSTALL = function()
      local f = io.open("dep_sel_marker.txt", "w")
      f:write("ran")
      f:close()
    end,
  }},
}}
"""
        top = f"""IDENTITY = "local.dep_sel_top@v1"
USER_MANAGED = true

DEPENDENCIES = {{{{
  {{{{ spec = "local.dep_sel_target@v1", source = "dep_sel_target.lua", setup = {{{{ "main" }}}} }}}}
}}}}

SETUP = {{{{
  main = {{{{
    CHECK = function() return true end,
    INSTALL = function() end,
  }}}},
}}}}
"""
        self.write_spec("dep_sel_target", dep)
        top_path = self.write_spec("dep_sel_top", top)
        manifest = self.create_manifest(
            f'PACKAGES = {{ {{ spec = "local.dep_sel_top@v1", source = "{top_path}" }} }}'
        )
        self.run_install(manifest)
        self.assertTrue(
            (self.test_dir / "dep_sel_marker.txt").exists(),
            "dependency entry's setup selection must run the dependency's pair",
        )

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
  {{{{ spec = "local.failing_pair@v1", source = "failing_pair.lua", setup = {{{{ "main" }}}} }}}}
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
            f'PACKAGES = {{ {{ spec = "local.dependent@v1", source = "{dep_path}", '
            f'setup = {{ "main" }} }} }}'
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
  {{{{ spec = "local.order_dep@v1", source = "order_dep.lua", setup = {{{{ "main" }}}} }}}}
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
            f'PACKAGES = {{ {{ spec = "local.order_top@v1", source = "{top_path}", '
            f'setup = {{ "main" }} }} }}'
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
            f'PACKAGES = {{ {{ spec = "local.concurrent_pair@v1", source = "{spec_path}", '
            f'setup = {{ "main" }} }} }}'
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

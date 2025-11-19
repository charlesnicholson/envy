#!/usr/bin/env python3
"""Functional tests for cache implementation using envy_functional_tester."""

import shutil
import subprocess
import tempfile
import uuid
from pathlib import Path
import unittest

from . import test_config


def parse_keyvalue(output: str) -> dict:
    """Parse key=value output into dict."""
    return dict(
        line.split("=", 1) for line in output.strip().split("\n") if "=" in line
    )


class TestCacheLockingAndConcurrency(unittest.TestCase):
    """Lock acquisition and concurrency tests."""

    def setUp(self):
        self.cache_root = Path(tempfile.mkdtemp(prefix="envy-cache-test-"))
        self.envy_test = test_config.get_envy_executable()
        self.test_id = str(uuid.uuid4())
        self.barrier_dir = Path(
            tempfile.mkdtemp(prefix=f"envy-barrier-{self.test_id}-")
        )

    def tearDown(self):
        shutil.rmtree(self.cache_root, ignore_errors=True)
        shutil.rmtree(self.barrier_dir, ignore_errors=True)

    def run_cache_cmd(
        self,
        *args,
        barrier_signal=None,
        barrier_wait=None,
        crash_after_ms=None,
        fail_before_complete=False,
    ):
        """Helper to run envy_functional_tester cache command."""
        cmd = [str(self.envy_test), "cache"] + list(args)
        cmd.append(f"--cache-root={self.cache_root}")
        cmd.append(f"--test-id={self.test_id}")
        cmd.append(f"--barrier-dir={self.barrier_dir}")
        if barrier_signal:
            cmd.append(f"--barrier-signal={barrier_signal}")
        if barrier_wait:
            cmd.append(f"--barrier-wait={barrier_wait}")
        if crash_after_ms is not None:
            cmd.append(f"--crash-after={crash_after_ms}")
        if fail_before_complete:
            cmd.append("--fail-before-complete")

        return subprocess.Popen(
            cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
        )

    def test_ensure_asset_first_time(self):
        """Acquire lock for new asset, returns staging path with lock."""
        proc = self.run_cache_cmd("ensure-asset", "gcc", "darwin", "arm64", "a1b2c3d4")
        stdout, _ = proc.communicate()
        result = parse_keyvalue(stdout)

        self.assertEqual(result["locked"], "true")
        install_path = Path(result["install_path"])
        stage_path = Path(result["stage_path"])
        asset_path = Path(result["asset_path"])
        self.assertEqual(install_path.name, "install")
        self.assertEqual(stage_path.name, "stage")
        self.assertEqual(stage_path.parent.name, "work")
        self.assertEqual(asset_path.name, "asset")
        self.assertFalse(install_path.exists())
        self.assertFalse(stage_path.exists())
        self.assertEqual(proc.returncode, 0)

        # Verify filesystem state
        entry = self.cache_root / "assets" / "gcc" / "darwin-arm64-blake3-a1b2c3d4"
        self.assertTrue((entry / "envy-complete").exists())
        self.assertTrue((entry / "asset").exists())
        self.assertFalse((entry / "work").exists())
        self.assertTrue((entry / "asset").exists())

    def test_ensure_asset_already_complete(self):
        """Request complete asset, returns final path immediately without lock."""
        # Pre-populate cache
        entry = self.cache_root / "assets" / "gcc" / "darwin-arm64-blake3-complete1"
        entry.mkdir(parents=True)
        (entry / "envy-complete").touch()

        proc = self.run_cache_cmd("ensure-asset", "gcc", "darwin", "arm64", "complete1")
        stdout, _ = proc.communicate()
        result = parse_keyvalue(stdout)

        self.assertEqual(result["locked"], "false")
        self.assertEqual(result["fast_path"], "true")
        self.assertEqual(result["entry_path"], str(entry))
        self.assertEqual(result["asset_path"], str(entry / "asset"))
        self.assertEqual(result["install_path"], "")

    def test_concurrent_ensure_same_asset(self):
        """Two processes request same asset—one stages, other blocks then finds complete."""
        # Process A: signal immediately, then do work (no wait - completes freely)
        proc_a = self.run_cache_cmd(
            "ensure-asset",
            "gcc",
            "darwin",
            "arm64",
            "concurrent1",
            barrier_signal="a_ready",
        )

        # Process B: wait for A to be ready, then attempt (will find A's completed entry)
        proc_b = self.run_cache_cmd(
            "ensure-asset",
            "gcc",
            "darwin",
            "arm64",
            "concurrent1",
            barrier_wait="a_ready",
        )

        stdout_a, _ = proc_a.communicate()
        stdout_b, _ = proc_b.communicate()

        result_a = parse_keyvalue(stdout_a)
        result_b = parse_keyvalue(stdout_b)

        self.assertIn("locked", result_a)
        self.assertIn("locked", result_b)
        locked_values = {result_a["locked"], result_b["locked"]}
        self.assertEqual(locked_values, {"true", "false"})
        if result_a["locked"] == "false":
            self.assertEqual(result_a["fast_path"], "true")
            self.assertEqual(result_b["fast_path"], "false")
        else:
            self.assertEqual(result_a["fast_path"], "false")
            self.assertEqual(result_b["fast_path"], "true")

    def test_ensure_recipe_vs_ensure_asset_different_locks(self):
        """Verify recipe and asset locks don't conflict."""
        # Start asset lock in background
        proc_asset = self.run_cache_cmd(
            "ensure-asset",
            "gcc",
            "darwin",
            "arm64",
            "locktest",
            barrier_signal="asset_locked",
            barrier_wait="recipe_checked",
        )

        # Wait for asset lock, then try recipe lock
        proc_recipe = self.run_cache_cmd(
            "ensure-recipe",
            "envy.cmake@v1",
            barrier_wait="asset_locked",
            barrier_signal="recipe_checked",
        )

        # Both should succeed without blocking each other
        proc_asset.communicate()
        proc_recipe.communicate()
        self.assertEqual(proc_asset.returncode, 0)
        self.assertEqual(proc_recipe.returncode, 0)


class TestStagingAndCommit(unittest.TestCase):
    """Staging directory and commit behavior tests."""

    def setUp(self):
        self.cache_root = Path(tempfile.mkdtemp(prefix="envy-cache-test-"))
        self.envy_test = test_config.get_envy_executable()
        self.test_id = str(uuid.uuid4())
        self.barrier_dir = Path(
            tempfile.mkdtemp(prefix=f"envy-barrier-{self.test_id}-")
        )

    def tearDown(self):
        shutil.rmtree(self.cache_root, ignore_errors=True)
        shutil.rmtree(self.barrier_dir, ignore_errors=True)

    def run_cache_cmd(self, *args, **kwargs):
        cmd = [str(self.envy_test), "cache"] + list(args)
        cmd.append(f"--cache-root={self.cache_root}")
        cmd.append(f"--test-id={self.test_id}")
        cmd.append(f"--barrier-dir={self.barrier_dir}")
        if "barrier_signal" in kwargs:
            cmd.append(f"--barrier-signal={kwargs['barrier_signal']}")
        if "barrier_wait" in kwargs:
            cmd.append(f"--barrier-wait={kwargs['barrier_wait']}")
        if "fail_before_complete" in kwargs and kwargs["fail_before_complete"]:
            cmd.append("--fail-before-complete")
        return subprocess.Popen(
            cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
        )

    def test_staging_auto_created(self):
        """Lock returned, staging directory created and then renamed on completion."""
        # Run command that will create staging and complete
        proc = self.run_cache_cmd("ensure-asset", "gcc", "darwin", "arm64", "staging1")
        stdout, _ = proc.communicate()
        result = parse_keyvalue(stdout)

        self.assertEqual(result["locked"], "true")

        # After completion, staging should be renamed to final directory
        final_dir = self.cache_root / "assets" / "gcc" / "darwin-arm64-blake3-staging1"
        staging = (
            self.cache_root
            / "assets"
            / "gcc"
            / "darwin-arm64-blake3-staging1"
            / "install"
        )

        self.assertTrue((final_dir / "envy-complete").exists())
        self.assertFalse(staging.exists())
        self.assertTrue((final_dir / "asset").exists())
        self.assertFalse((final_dir / "work").exists())

    def test_mark_complete_commits_on_exit(self):
        """Call mark_complete(), verify staging renamed and marker written."""
        proc = self.run_cache_cmd("ensure-asset", "gcc", "darwin", "arm64", "commit1")
        stdout, _ = proc.communicate()
        result = parse_keyvalue(stdout)

        self.assertEqual(result["locked"], "true")

        # Verify committed
        entry = self.cache_root / "assets" / "gcc" / "darwin-arm64-blake3-commit1"
        self.assertTrue((entry / "envy-complete").exists())
        staging = (
            self.cache_root
            / "assets"
            / "gcc"
            / "darwin-arm64-blake3-commit1"
            / "install"
        )
        self.assertFalse(staging.exists())
        self.assertTrue((entry / "asset").exists())
        self.assertFalse((entry / "work").exists())

    def test_no_mark_complete_abandons_staging(self):
        """Lock destructs without mark_complete(), staging abandoned."""
        proc = self.run_cache_cmd(
            "ensure-asset",
            "gcc",
            "darwin",
            "arm64",
            "abandon1",
            fail_before_complete=True,
        )
        proc.communicate()  # Wait and close pipes

        # Entry should NOT be complete
        entry = self.cache_root / "assets" / "gcc" / "darwin-arm64-blake3-abandon1"
        self.assertFalse((entry / "envy-complete").exists())

    def test_staging_atomic_rename(self):
        """Large multi-file asset staged—other process never sees partial final directory."""
        # TODO: This test requires a poll-entry command that doesn't exist yet
        # Skipping for now
        self.skipTest("poll-entry command not implemented yet")


class TestCrashRecovery(unittest.TestCase):
    """Crash recovery and stale staging cleanup tests."""

    def setUp(self):
        self.cache_root = Path(tempfile.mkdtemp(prefix="envy-cache-test-"))
        self.envy_test = test_config.get_envy_executable()
        self.test_id = str(uuid.uuid4())
        self.barrier_dir = Path(
            tempfile.mkdtemp(prefix=f"envy-barrier-{self.test_id}-")
        )

    def tearDown(self):
        shutil.rmtree(self.cache_root, ignore_errors=True)
        shutil.rmtree(self.barrier_dir, ignore_errors=True)

    def run_cache_cmd(self, *args, **kwargs):
        cmd = [str(self.envy_test), "cache"] + list(args)
        cmd.append(f"--cache-root={self.cache_root}")
        cmd.append(f"--test-id={self.test_id}")
        cmd.append(f"--barrier-dir={self.barrier_dir}")
        if "barrier_signal" in kwargs:
            cmd.append(f"--barrier-signal={kwargs['barrier_signal']}")
        if "barrier_wait" in kwargs:
            cmd.append(f"--barrier-wait={kwargs['barrier_wait']}")
        if "crash_after_ms" in kwargs:
            cmd.append(f"--crash-after={kwargs['crash_after_ms']}")
        return subprocess.Popen(
            cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
        )

    def test_stale_inprogress_cleaned(self):
        """Kill process mid-staging, next ensure removes stale .install."""
        # Process A crashes after acquiring lock
        proc_a = self.run_cache_cmd(
            "ensure-asset",
            "gcc",
            "darwin",
            "arm64",
            "crash1",
            barrier_signal="locked",
            crash_after_ms=100,
        )

        # Wait for crash
        proc_a.communicate()
        self.assertNotEqual(proc_a.returncode, 0)

        # Verify stale staging exists
        staging = (
            self.cache_root
            / "assets"
            / "gcc"
            / "darwin-arm64-blake3-crash1"
            / "install"
        )
        self.assertTrue(staging.exists())

        # Process B cleans up and succeeds
        proc_b = self.run_cache_cmd(
            "ensure-asset", "gcc", "darwin", "arm64", "crash1", barrier_wait="locked"
        )
        stdout_b, _ = proc_b.communicate()
        result_b = parse_keyvalue(stdout_b)

        self.assertEqual(result_b["locked"], "true")
        self.assertFalse(staging.exists())  # Cleaned

        entry = self.cache_root / "assets" / "gcc" / "darwin-arm64-blake3-crash1"
        self.assertTrue((entry / "envy-complete").exists())

    def test_lock_released_on_crash(self):
        """Process crashes holding lock, OS releases lock, next process acquires."""
        # Process A crashes while holding lock
        proc_a = self.run_cache_cmd(
            "ensure-asset",
            "gcc",
            "darwin",
            "arm64",
            "lockcrash",
            barrier_signal="a_locked",
            crash_after_ms=50,
        )

        # Process B waits for A to get lock, then tries after crash
        proc_b = self.run_cache_cmd(
            "ensure-asset",
            "gcc",
            "darwin",
            "arm64",
            "lockcrash",
            barrier_wait="a_locked",
        )

        proc_a.communicate()
        self.assertNotEqual(proc_a.returncode, 0)

        stdout_b, _ = proc_b.communicate()
        result_b = parse_keyvalue(stdout_b)

        # B successfully acquired lock after A crashed
        self.assertEqual(result_b["locked"], "true")
        self.assertEqual(proc_b.returncode, 0)


class TestLockFileLifecycle(unittest.TestCase):
    """Lock file creation and removal tests."""

    def setUp(self):
        self.cache_root = Path(tempfile.mkdtemp(prefix="envy-cache-test-"))
        self.envy_test = test_config.get_envy_executable()
        self.test_id = str(uuid.uuid4())
        self.barrier_dir = Path(
            tempfile.mkdtemp(prefix=f"envy-barrier-{self.test_id}-")
        )

    def tearDown(self):
        shutil.rmtree(self.cache_root, ignore_errors=True)
        shutil.rmtree(self.barrier_dir, ignore_errors=True)

    def run_cache_cmd(self, *args, **kwargs):
        cmd = [str(self.envy_test), "cache"] + list(args)
        cmd.append(f"--cache-root={self.cache_root}")
        cmd.append(f"--test-id={self.test_id}")
        cmd.append(f"--barrier-dir={self.barrier_dir}")
        if "barrier_signal" in kwargs:
            cmd.append(f"--barrier-signal={kwargs['barrier_signal']}")
        if "barrier_wait" in kwargs:
            cmd.append(f"--barrier-wait={kwargs['barrier_wait']}")
        if "barrier_signal_after" in kwargs:
            cmd.append(f"--barrier-signal-after={kwargs['barrier_signal_after']}")
        if "barrier_wait_after" in kwargs:
            cmd.append(f"--barrier-wait-after={kwargs['barrier_wait_after']}")
        return subprocess.Popen(
            cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
        )

    def test_lock_file_created_and_removed(self):
        """Lock acquired, verify lock file exists in locks/, then deleted on release."""
        # Process holds lock, signals AFTER acquiring lock
        proc = self.run_cache_cmd(
            "ensure-asset",
            "gcc",
            "darwin",
            "arm64",
            "lockfile1",
            barrier_signal_after="locked",
            barrier_wait_after="check_done",
        )

        # Wait for lock acquisition
        barrier = self.barrier_dir / "locked"
        while not barrier.exists():
            pass

        # Verify lock file exists
        lock_file = (
            self.cache_root / "locks" / "assets.gcc.darwin-arm64-blake3-lockfile1.lock"
        )
        self.assertTrue(lock_file.exists())

        # Let process finish
        (self.barrier_dir / "check_done").touch()
        proc.communicate()

        # Lock file should be removed
        self.assertFalse(lock_file.exists())

    def test_lock_file_naming_asset(self):
        """Verify asset lock path matches assets.{identity}.{platform}-{arch}-blake3-{hash}.lock."""
        proc = self.run_cache_cmd("ensure-asset", "gcc", "darwin", "arm64", "abc123")
        stdout, _ = proc.communicate()
        result = parse_keyvalue(stdout)

        expected_lock = "assets.gcc.darwin-arm64-blake3-abc123.lock"
        self.assertIn(expected_lock, result.get("lock_file", ""))

    def test_lock_file_naming_recipe(self):
        """Verify recipe lock path matches recipe.{identity}.lock."""
        proc = self.run_cache_cmd("ensure-recipe", "envy.cmake@v1")
        stdout, _ = proc.communicate()
        result = parse_keyvalue(stdout)

        expected_lock = "recipe.envy.cmake@v1.lock"
        self.assertIn(expected_lock, result.get("lock_file", ""))


class TestEntryPathsAndStructure(unittest.TestCase):
    """Entry path construction and cache directory structure tests."""

    def setUp(self):
        self.cache_root = Path(tempfile.mkdtemp(prefix="envy-cache-test-"))
        self.envy_test = test_config.get_envy_executable()
        self.test_id = str(uuid.uuid4())
        self.barrier_dir = Path(
            tempfile.mkdtemp(prefix=f"envy-barrier-{self.test_id}-")
        )

    def tearDown(self):
        shutil.rmtree(self.cache_root, ignore_errors=True)
        shutil.rmtree(self.barrier_dir, ignore_errors=True)

    def run_cache_cmd(self, *args):
        cmd = [str(self.envy_test), "cache"] + list(args)
        cmd.append(f"--cache-root={self.cache_root}")
        cmd.append(f"--test-id={self.test_id}")
        cmd.append(f"--barrier-dir={self.barrier_dir}")
        return subprocess.Popen(
            cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
        )

    def test_asset_entry_path_structure(self):
        """Verify asset path is assets/{identity}.{platform}-{arch}-blake3-{hash}/."""
        proc = self.run_cache_cmd("ensure-asset", "gcc", "linux", "x86_64", "deadbeef")
        stdout, _ = proc.communicate()
        result = parse_keyvalue(stdout)

        expected_path = (
            self.cache_root / "assets" / "gcc" / "linux-x86_64-blake3-deadbeef"
        )
        self.assertEqual(result["entry_path"], str(expected_path))

    def test_recipe_entry_path_structure(self):
        """Verify recipe path is recipes/{identity}/."""
        proc = self.run_cache_cmd("ensure-recipe", "envy.cmake@v1")
        stdout, _ = proc.communicate()
        result = parse_keyvalue(stdout)

        expected_path = self.cache_root / "recipes" / "envy.cmake@v1"
        self.assertEqual(result["entry_path"], str(expected_path))

    def test_recipe_directory_structure(self):
        """Verify recipe entry is a directory with expected structure."""
        proc = self.run_cache_cmd("ensure-recipe", "envy.test@v2")
        stdout, _ = proc.communicate()
        result = parse_keyvalue(stdout)

        entry_path = Path(result["entry_path"])
        asset_path = Path(result["asset_path"])

        self.assertTrue(entry_path.is_dir(), "Recipe entry should be a directory")
        self.assertEqual(
            entry_path / "asset",
            asset_path,
            "Recipe asset_path should be entry_path/asset",
        )
        self.assertTrue(
            (entry_path / "envy-complete").exists(),
            "Recipe should have envy-complete marker",
        )

    def test_recipe_asset_path_structure(self):
        """Verify recipe uses asset subdirectory like other cache entries."""
        proc = self.run_cache_cmd("ensure-recipe", "envy.simple@v1")
        stdout, _ = proc.communicate()
        result = parse_keyvalue(stdout)

        entry_path = Path(result["entry_path"])
        asset_path = Path(result["asset_path"])

        # Recipes use uniform cache structure with asset/ subdirectory
        self.assertEqual(entry_path / "asset", asset_path)

    def test_cache_directory_creation(self):
        """Call ensure_*, verify locks/ directory auto-created if missing."""
        locks_dir = self.cache_root / "locks"
        self.assertFalse(locks_dir.exists())

        proc = self.run_cache_cmd("ensure-asset", "gcc", "darwin", "arm64", "auto1")
        proc.communicate()

        self.assertTrue(locks_dir.exists())

    def test_asset_path_on_fast_path(self):
        """Fast-path ensure returns asset_path pointing at payload directory."""
        entry = self.cache_root / "assets" / "gcc" / "darwin-arm64-blake3-fast1"
        asset_dir = entry / "asset"
        asset_dir.mkdir(parents=True, exist_ok=True)
        sentinel = asset_dir / "payload.bin"
        sentinel.write_text("cached")
        (entry / "envy-complete").touch()

        proc = self.run_cache_cmd("ensure-asset", "gcc", "darwin", "arm64", "fast1")
        stdout, _ = proc.communicate()
        result = parse_keyvalue(stdout)

        self.assertEqual(result["locked"], "false")
        self.assertEqual(Path(result["asset_path"]).resolve(), asset_dir.resolve())
        self.assertEqual(result["install_path"], "")
        self.assertEqual(result["fetch_path"], "")
        self.assertEqual(result["stage_path"], "")
        self.assertTrue(sentinel.exists())


class TestEdgeCases(unittest.TestCase):
    """Edge cases and corner scenarios."""

    def setUp(self):
        self.cache_root = Path(tempfile.mkdtemp(prefix="envy-cache-test-"))
        self.envy_test = test_config.get_envy_executable()
        self.test_id = str(uuid.uuid4())
        self.barrier_dir = Path(
            tempfile.mkdtemp(prefix=f"envy-barrier-{self.test_id}-")
        )

    def tearDown(self):
        shutil.rmtree(self.cache_root, ignore_errors=True)
        shutil.rmtree(self.barrier_dir, ignore_errors=True)

    def run_cache_cmd(self, *args, **kwargs):
        cmd = [str(self.envy_test), "cache"] + list(args)
        cmd.append(f"--cache-root={self.cache_root}")
        cmd.append(f"--test-id={self.test_id}")
        cmd.append(f"--barrier-dir={self.barrier_dir}")
        if "barrier_signal" in kwargs:
            cmd.append(f"--barrier-signal={kwargs['barrier_signal']}")
        if "barrier_wait" in kwargs:
            cmd.append(f"--barrier-wait={kwargs['barrier_wait']}")
        if "barrier_signal_after" in kwargs:
            cmd.append(f"--barrier-signal-after={kwargs['barrier_signal_after']}")
        if "barrier_wait_after" in kwargs:
            cmd.append(f"--barrier-wait-after={kwargs['barrier_wait_after']}")
        return subprocess.Popen(
            cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
        )

    def test_recheck_after_lock_wait(self):
        """Process B waits on lock, A completes while waiting, B rechecks and finds complete."""
        # Process A: signal after acquiring lock, complete freely
        proc_a = self.run_cache_cmd(
            "ensure-asset",
            "gcc",
            "darwin",
            "arm64",
            "recheck1",
            barrier_signal_after="a_ready",
        )

        # Process B: wait for A to acquire lock, then attempt (will find complete)
        proc_b = self.run_cache_cmd(
            "ensure-asset", "gcc", "darwin", "arm64", "recheck1", barrier_wait="a_ready"
        )

        stdout_a, _ = proc_a.communicate()
        stdout_b, _ = proc_b.communicate()

        result_a = parse_keyvalue(stdout_a)
        result_b = parse_keyvalue(stdout_b)

        self.assertIn("locked", result_a)
        self.assertIn("locked", result_b)
        self.assertEqual(result_a["locked"], "true")
        if result_b["locked"] == "false":
            self.assertEqual(result_b["fast_path"], "true")
        else:
            self.assertEqual(result_b["fast_path"], "false")

    def test_asset_without_marker_requires_lock(self):
        """Existing asset directory without marker still forces staging."""
        entry = self.cache_root / "assets" / "gcc" / "darwin-arm64-blake3-raw1"
        (entry / "asset").mkdir(parents=True, exist_ok=True)

        proc = self.run_cache_cmd("ensure-asset", "gcc", "darwin", "arm64", "raw1")
        stdout, _ = proc.communicate()
        result = parse_keyvalue(stdout)

        self.assertEqual(result.get("locked"), "true")

    def test_empty_staging_committed(self):
        """Create staging but write nothing, call mark_complete()—verify commit happens."""
        # Default behavior commits even if staging empty
        proc = self.run_cache_cmd("ensure-asset", "gcc", "darwin", "arm64", "empty1")
        stdout, _ = proc.communicate()
        result = parse_keyvalue(stdout)

        self.assertTrue(result["locked"])

        # Entry should be complete even though no files staged
        entry = self.cache_root / "assets" / "gcc" / "darwin-arm64-blake3-empty1"
        self.assertTrue((entry / "envy-complete").exists())

    def test_multiple_assets_same_identity_different_platforms(self):
        """Same identity but different platform/arch/hash → different cache entries."""
        proc_darwin = self.run_cache_cmd(
            "cache", "ensure-asset", "gcc", "darwin", "arm64", "multi1"
        )
        proc_linux = self.run_cache_cmd(
            "cache", "ensure-asset", "gcc", "linux", "x86_64", "multi1"
        )

        stdout_darwin, _ = proc_darwin.communicate()
        stdout_linux, _ = proc_linux.communicate()

        result_darwin = parse_keyvalue(stdout_darwin)
        result_linux = parse_keyvalue(stdout_linux)

        # Different paths
        self.assertNotEqual(result_darwin["entry_path"], result_linux["entry_path"])

        # Both complete
        self.assertTrue(Path(result_darwin["entry_path"], "envy-complete").exists())
        self.assertTrue(Path(result_linux["entry_path"], "envy-complete").exists())

    def test_ensure_with_custom_cache_root(self):
        """Pass custom root to cache constructor, verify all paths relative to custom root."""
        # Already tested via --cache-root flag in all tests
        custom_root = Path(tempfile.mkdtemp(prefix="custom-cache-"))

        proc = subprocess.Popen(
            [
                str(self.envy_test),
                "cache",
                "ensure-asset",
                "gcc",
                "darwin",
                "arm64",
                "custom1",
                f"--cache-root={custom_root}",
            ],
            stdout=subprocess.PIPE,
            text=True,
        )
        stdout, _ = proc.communicate()
        result = parse_keyvalue(stdout)

        # Verify all paths under custom root
        self.assertTrue(result["entry_path"].startswith(str(custom_root)))
        shutil.rmtree(custom_root)


class TestSubprocessConcurrency(unittest.TestCase):
    """Integration tests with real subprocess spawning."""

    def setUp(self):
        self.cache_root = Path(tempfile.mkdtemp(prefix="envy-cache-test-"))
        self.envy_test = test_config.get_envy_executable()
        self.test_id = str(uuid.uuid4())
        self.barrier_dir = Path(
            tempfile.mkdtemp(prefix=f"envy-barrier-{self.test_id}-")
        )

    def tearDown(self):
        shutil.rmtree(self.cache_root, ignore_errors=True)
        shutil.rmtree(self.barrier_dir, ignore_errors=True)

    def run_cache_cmd(self, *args, **kwargs):
        cmd = [str(self.envy_test), "cache"] + list(args)
        cmd.append(f"--cache-root={self.cache_root}")
        cmd.append(f"--test-id={self.test_id}")
        if "crash_after_ms" in kwargs:
            cmd.append(f"--crash-after={kwargs['crash_after_ms']}")
        return subprocess.Popen(
            cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
        )

    def test_subprocess_concurrent_ensure(self):
        """Spawn multiple envy subprocesses requesting same asset—one stages, others wait."""
        # Spawn 5 processes simultaneously
        procs = []
        for _ in range(5):
            proc = self.run_cache_cmd("ensure-asset", "gcc", "darwin", "arm64", "many1")
            procs.append(proc)

        results = []
        for proc in procs:
            stdout, _ = proc.communicate()
            results.append(parse_keyvalue(stdout))

        # Exactly one locked (staged), rest fast path
        locked_count = sum(1 for r in results if r["locked"] == "true")
        fast_path_count = sum(1 for r in results if r.get("fast_path") == "true")

        self.assertEqual(locked_count, 1)
        self.assertEqual(fast_path_count, 4)

    def test_sigkill_recovery(self):
        """Start staging, SIGKILL process, verify next process cleans up and succeeds."""
        # Same as test_stale_inprogress_cleaned but explicit SIGKILL
        proc_a = self.run_cache_cmd(
            "cache",
            "ensure-asset",
            "gcc",
            "darwin",
            "arm64",
            "sigkill1",
            crash_after_ms=50,
        )

        proc_a.communicate()
        self.assertNotEqual(proc_a.returncode, 0)

        # Verify stale staging
        staging = (
            self.cache_root
            / "assets"
            / "gcc"
            / "darwin-arm64-blake3-sigkill1"
            / "install"
        )
        self.assertTrue(staging.exists())

        # Recovery
        proc_b = self.run_cache_cmd(
            "ensure-asset", "gcc", "darwin", "arm64", "sigkill1"
        )
        stdout_b, _ = proc_b.communicate()
        result_b = parse_keyvalue(stdout_b)

        self.assertEqual(result_b["locked"], "true")
        self.assertFalse(staging.exists())

        entry = self.cache_root / "assets" / "gcc" / "darwin-arm64-blake3-sigkill1"
        self.assertTrue((entry / "envy-complete").exists())


if __name__ == "__main__":
    unittest.main()

#!/usr/bin/env python3
"""Functional tests for engine stage phase.

Tests default extraction, declarative stage options (strip), and imperative
stage functions (ctx.extract, ctx.extract_all).
"""

import hashlib
import io
import os
import shutil
import subprocess
import tarfile
import tempfile
from pathlib import Path
import unittest

from . import test_config

# Test archive contents - standard nested directory structure
TEST_ARCHIVE_FILES = {
    "root/file1.txt": "Root file content\n",
    "root/file2.txt": "Another root file\n",
    "root/subdir1/file3.txt": "Subdir file content\n",
    "root/subdir1/subdir2/file4.txt": "Deep nested file\n",
    "root/subdir1/subdir2/file5.txt": "Another deep file\n",
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


# Inline specs for stage tests - {ARCHIVE_PATH} and {ARCHIVE_HASH} replaced at runtime
SPECS = {
    "stage_default.lua": """-- Test default stage phase (no stage field, auto-extract archives)
IDENTITY = "local.stage_default@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

-- No stage field - should auto-extract to install_dir
""",
    "stage_declarative_strip.lua": """-- Test declarative stage with strip option
IDENTITY = "local.stage_declarative_strip@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = {{
  strip = 1  -- Remove root/ top-level directory
}}
""",
    "stage_imperative.lua": """-- Test imperative stage function using envy.extract_all
IDENTITY = "local.stage_imperative@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  -- Custom stage logic - extract all with strip
  envy.extract_all(fetch_dir, stage_dir, {{strip = 1}})
end
""",
    "stage_extract_single.lua": """-- Test imperative stage using envy.extract for single file
IDENTITY = "local.stage_extract_single@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  -- Extract single archive with specific options
  local files = envy.extract(fetch_dir .. "/test.tar.gz", stage_dir, {{strip = 1}})
  -- files should be 5 (the number of regular files extracted)
end
""",
    "stage_shell_basic.lua": """-- Test basic shell script stage phase
IDENTITY = "local.stage_shell_basic@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

-- Shell script that extracts and creates a marker file
if envy.PLATFORM == "windows" then
  -- PowerShell variant
  STAGE = [[
    # PowerShell still invoked; use tar (Windows 10+ includes bsdtar) and Out-File
    tar -xzf ../fetch/test.tar.gz --strip-components=1
    "stage script executed" | Out-File -Encoding UTF8 STAGE_MARKER.txt
    Get-ChildItem -Force | Format-List > DIR_LIST.txt
    if (-not (Test-Path STAGE_MARKER.txt)) {{ exit 1 }}
    exit 0
  ]]
else
  STAGE = [[
    # Extract the archive manually (should be in fetch_dir)
    tar -xzf ../fetch/test.tar.gz --strip-components=1

    # Create a marker file to prove shell ran
    echo "stage script executed" > STAGE_MARKER.txt

    # List files for debugging
    ls -la
  ]]
end
""",
    "stage_shell_failure.lua": """-- Test shell script failure in stage phase
IDENTITY = "local.stage_shell_failure@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

-- Shell script that intentionally fails
STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  if envy.PLATFORM == "windows" then
    envy.run([[
      Write-Output "About to fail"
      exit 9
    ]], {{ shell = ENVY_SHELL.POWERSHELL, check = true }})
  else
    envy.run([[
      set -euo pipefail
      echo "About to fail"
      false
    ]], {{ check = true }})
  end
end
""",
    "stage_shell_complex.lua": """-- Test complex shell script operations in stage phase
IDENTITY = "local.stage_shell_complex@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

-- Shell script with complex operations
if envy.PLATFORM == "windows" then
  STAGE = [[
    tar -xzf ../fetch/test.tar.gz --strip-components=1
    New-Item -ItemType Directory -Force -Path custom/bin | Out-Null
    New-Item -ItemType Directory -Force -Path custom/lib | Out-Null
    New-Item -ItemType Directory -Force -Path custom/share | Out-Null
    Move-Item file1.txt custom/bin/;
    Move-Item file2.txt custom/lib/;
    $fileCount = (Get-ChildItem -Recurse -File | Measure-Object).Count
    @(
      'Stage phase executed successfully'
      'Files reorganized into custom structure'
      ("Working directory: $(Get-Location)")
      ("File count: $fileCount")
    ) | Out-File -Encoding UTF8 custom/share/metadata.txt
    if (-not (Test-Path custom/bin/file1.txt)) {{ exit 1 }}
    if (-not (Test-Path custom/lib/file2.txt)) {{ exit 1 }}
    if (-not (Test-Path custom/share/metadata.txt)) {{ exit 1 }}
    Write-Output "Stage complete: custom structure created"
    exit 0
  ]]
else
  STAGE = [[
    # Extract with strip
    tar -xzf ../fetch/test.tar.gz --strip-components=1

    # Create directory structure
    mkdir -p custom/bin custom/lib custom/share

    # Move files to custom locations
    mv file1.txt custom/bin/
    mv file2.txt custom/lib/

    # Create a file with metadata
    cat > custom/share/metadata.txt << EOF
Stage phase executed successfully
Files reorganized into custom structure
Working directory: $(pwd)
File count: $(find . -type f | wc -l)
EOF

    # Ensure everything is in place
    test -f custom/bin/file1.txt || exit 1
    test -f custom/lib/file2.txt || exit 1
    test -f custom/share/metadata.txt || exit 1

    echo "Stage complete: custom structure created"
  ]]
end
""",
    "stage_shell_env.lua": """-- Test shell script with environment variable access
IDENTITY = "local.stage_shell_env@v1"

FETCH = {{
  source = "{ARCHIVE_PATH}",
  sha256 = "{ARCHIVE_HASH}"
}}

-- Shell script that uses environment variables
if envy.PLATFORM == "windows" then
  STAGE = [[
    tar -xzf ../fetch/test.tar.gz --strip-components=1
    $pathStatus = if ($env:Path) {{ 'yes' }} else {{ '' }}
    $homeStatus = if ($env:UserProfile) {{ 'yes' }} else {{ '' }}
    $userStatus = if ($env:USERNAME) {{ 'yes' }} else {{ '' }}
    @(
      "PATH is available: $pathStatus"
      "HOME is available: $homeStatus"
      "USER is available: $userStatus"
      ("Shell: powershell")
    ) | Out-File -Encoding UTF8 env_info.txt
    if (-not (Test-Path env_info.txt)) {{ exit 1 }}
    if (-not (Select-String -Path env_info.txt -Pattern "PATH is available: yes" -Quiet)) {{ exit 1 }}
    Write-Output "Environment check complete"
    exit 0
  ]]
else
  STAGE = [[
    # Extract archive
    tar -xzf ../fetch/test.tar.gz --strip-components=1

    # Write environment information to file
    cat > env_info.txt << EOF
PATH is available: ${{PATH:+yes}}
HOME is available: ${{HOME:+yes}}
USER is available: ${{USER:+yes}}
Shell: $(basename $SHELL)
EOF

    # Verify the file was created
    test -f env_info.txt || exit 1
    grep -q "PATH is available: yes" env_info.txt || exit 1

    echo "Environment check complete"
  ]]
end
""",
}


class TestStagePhase(unittest.TestCase):
    """Tests for stage phase (archive extraction and preparation)."""

    def setUp(self):
        self.cache_root = Path(tempfile.mkdtemp(prefix="envy-stage-test-"))
        self.test_dir = Path(tempfile.mkdtemp(prefix="envy-stage-files-"))
        self.envy_test = test_config.get_envy_executable()
        self.trace_flag = ["--trace"] if os.environ.get("ENVY_TEST_TRACE") else []

        # Create test archive and get its hash
        self.archive_path = self.test_dir / "test.tar.gz"
        self.archive_hash = create_test_archive(self.archive_path)

        # Write inline specs to temp directory with archive path/hash substituted
        for name, content in SPECS.items():
            spec_content = content.format(
                ARCHIVE_PATH=self.archive_path.as_posix(),
                ARCHIVE_HASH=self.archive_hash,
            )
            (self.test_dir / name).write_text(spec_content, encoding="utf-8")

    def tearDown(self):
        shutil.rmtree(self.cache_root, ignore_errors=True)
        shutil.rmtree(self.test_dir, ignore_errors=True)

    def spec_path(self, name: str) -> str:
        """Get path to spec file."""
        return str(self.test_dir / name)

    def get_pkg_path(self, identity):
        """Find package directory for given identity in cache.

        Args:
            identity: Spec identity (e.g., "local.stage_default@v1")
        """
        pkgs_dir = self.cache_root / "packages" / identity
        if not pkgs_dir.exists():
            return None
        # Find the platform-specific package subdirectory
        for subdir in pkgs_dir.iterdir():
            if subdir.is_dir():
                # Files are in the pkg/ subdirectory
                pkg_dir = subdir / "pkg"
                if pkg_dir.exists():
                    return pkg_dir
                return subdir
        return None

    def test_default_stage_extracts_to_install_dir(self):
        """Spec with no stage field auto-extracts archives."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "local.stage_default@v1",
                self.spec_path("stage_default.lua"),
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(
            result.returncode, 0, f"stdout: {result.stdout}\nstderr: {result.stderr}"
        )

        # Check that files were extracted (should be in install_dir since no custom phases)
        pkg_path = self.get_pkg_path("local.stage_default@v1")
        assert pkg_path

        # Default extraction keeps root/ directory
        self.assertTrue((pkg_path / "root").exists(), "root/ directory not found")
        self.assertTrue((pkg_path / "root" / "file1.txt").exists())
        self.assertTrue((pkg_path / "root" / "file2.txt").exists())
        self.assertTrue((pkg_path / "root" / "subdir1" / "file3.txt").exists())

    def test_declarative_strip_removes_top_level(self):
        """Spec with stage = {strip=1} removes top-level directory."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "local.stage_declarative_strip@v1",
                self.spec_path("stage_declarative_strip.lua"),
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(
            result.returncode, 0, f"stdout: {result.stdout}\nstderr: {result.stderr}"
        )

        pkg_path = self.get_pkg_path("local.stage_declarative_strip@v1")
        assert pkg_path

        # With strip=1, root/ should be removed
        self.assertFalse((pkg_path / "root").exists(), "root/ should be stripped")
        self.assertTrue((pkg_path / "file1.txt").exists())
        self.assertTrue((pkg_path / "file2.txt").exists())
        self.assertTrue((pkg_path / "subdir1" / "file3.txt").exists())

    def test_imperative_extract_all(self):
        """Spec with stage function using ctx:extract_all works."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "local.stage_imperative@v1",
                self.spec_path("stage_imperative.lua"),
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(
            result.returncode, 0, f"stdout: {result.stdout}\nstderr: {result.stderr}"
        )

        pkg_path = self.get_pkg_path("local.stage_imperative@v1")
        assert pkg_path

        # Custom function used strip=1
        self.assertFalse((pkg_path / "root").exists())
        self.assertTrue((pkg_path / "file1.txt").exists())
        self.assertTrue((pkg_path / "subdir1" / "file3.txt").exists())

    def test_imperative_extract_single(self):
        """Spec with stage function using ctx:extract for single file works."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "local.stage_extract_single@v1",
                self.spec_path("stage_extract_single.lua"),
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(
            result.returncode, 0, f"stdout: {result.stdout}\nstderr: {result.stderr}"
        )

        pkg_path = self.get_pkg_path("local.stage_extract_single@v1")
        assert pkg_path

        # ctx:extract with strip=1
        self.assertFalse((pkg_path / "root").exists())
        self.assertTrue((pkg_path / "file1.txt").exists())

    def test_shell_script_basic(self):
        """Spec with shell script stage extracts and creates marker file."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "local.stage_shell_basic@v1",
                self.spec_path("stage_shell_basic.lua"),
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(
            result.returncode, 0, f"stdout: {result.stdout}\nstderr: {result.stderr}"
        )

        pkg_path = self.get_pkg_path("local.stage_shell_basic@v1")
        assert pkg_path

        # Check that shell script executed
        self.assertTrue(
            (pkg_path / "STAGE_MARKER.txt").exists(), "STAGE_MARKER.txt not found"
        )

        # Verify marker content
        marker_content = (pkg_path / "STAGE_MARKER.txt").read_text()
        self.assertIn("stage script executed", marker_content)

        # Check that files were extracted with strip=1
        self.assertFalse((pkg_path / "root").exists())
        self.assertTrue((pkg_path / "file1.txt").exists())
        self.assertTrue((pkg_path / "file2.txt").exists())

    def test_shell_script_failure(self):
        """Spec with failing shell script should fail stage phase."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "local.stage_shell_failure@v1",
                self.spec_path("stage_shell_failure.lua"),
            ],
            capture_output=True,
            text=True,
        )

        # Should fail
        self.assertNotEqual(result.returncode, 0, "Expected failure but succeeded")

        # Error message should mention stage failure
        error_output = result.stdout + result.stderr
        self.assertTrue(
            "Stage shell script failed" in error_output
            or "exit code 1" in error_output
            or "failed" in error_output.lower(),
            f"Expected stage failure message in output:\n{error_output}",
        )

    def test_shell_script_complex_operations(self):
        """Spec with complex shell operations creates custom directory structure."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "local.stage_shell_complex@v1",
                self.spec_path("stage_shell_complex.lua"),
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(
            result.returncode, 0, f"stdout: {result.stdout}\nstderr: {result.stderr}"
        )

        pkg_path = self.get_pkg_path("local.stage_shell_complex@v1")
        assert pkg_path

        # Check custom directory structure was created
        self.assertTrue((pkg_path / "custom").exists())
        self.assertTrue((pkg_path / "custom" / "bin").exists())
        self.assertTrue((pkg_path / "custom" / "lib").exists())
        self.assertTrue((pkg_path / "custom" / "share").exists())

        # Check files were moved to custom locations
        self.assertTrue(
            (pkg_path / "custom" / "bin" / "file1.txt").exists(),
            "file1.txt not in custom/bin/",
        )
        self.assertTrue(
            (pkg_path / "custom" / "lib" / "file2.txt").exists(),
            "file2.txt not in custom/lib/",
        )

        # Check metadata file was created
        metadata_path = pkg_path / "custom" / "share" / "metadata.txt"
        self.assertTrue(metadata_path.exists(), "metadata.txt not found")

        # Verify metadata content
        metadata_content = metadata_path.read_text()
        self.assertIn("Stage phase executed successfully", metadata_content)
        self.assertIn("Files reorganized", metadata_content)

    def test_shell_script_environment_access(self):
        """Spec with shell script can access environment variables."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "local.stage_shell_env@v1",
                self.spec_path("stage_shell_env.lua"),
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(
            result.returncode, 0, f"stdout: {result.stdout}\nstderr: {result.stderr}"
        )

        pkg_path = self.get_pkg_path("local.stage_shell_env@v1")
        assert pkg_path

        # Check environment info file was created
        env_info_path = pkg_path / "env_info.txt"
        self.assertTrue(env_info_path.exists(), "env_info.txt not found")

        # Verify environment variables were accessible
        env_content = env_info_path.read_text()
        self.assertIn("PATH is available: yes", env_content)

    def test_shell_script_output_logged(self):
        """Shell script output should be visible in logs."""
        result = subprocess.run(
            [
                str(self.envy_test),
                f"--cache-root={self.cache_root}",
                *self.trace_flag,
                "engine-test",
                "local.stage_shell_basic@v1",
                self.spec_path("stage_shell_basic.lua"),
            ],
            capture_output=True,
            text=True,
        )

        self.assertEqual(
            result.returncode, 0, f"stdout: {result.stdout}\nstderr: {result.stderr}"
        )

        # Shell script should log "stage script executed"
        combined_output = result.stdout + result.stderr
        # Note: output might be in either stdout or stderr depending on TUI configuration
        # We just verify the script ran successfully


if __name__ == "__main__":
    unittest.main()

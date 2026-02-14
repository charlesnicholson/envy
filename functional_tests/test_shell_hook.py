"""Integration tests for envy shell hook behavior."""

import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from . import test_config


def _get_envy_binary() -> Path:
    root = Path(__file__).parent.parent / "out" / "build"
    if sys.platform == "win32":
        return root / "envy.exe"
    return root / "envy"


@unittest.skipIf(sys.platform == "win32", "bash hook tests require Unix")
class TestBashHook(unittest.TestCase):
    """Test bash shell hook behavior by sourcing and invoking _envy_hook."""

    def setUp(self) -> None:
        self._temp_dir = Path(tempfile.mkdtemp(prefix="envy-bash-hook-test-"))
        self._cache_dir = self._temp_dir / "cache"
        self._envy = _get_envy_binary()

        # Trigger self-deploy to get hook files
        project_dir = self._temp_dir / "setup-project"
        bin_dir = self._temp_dir / "setup-bin"
        env = test_config.get_test_env()
        env["ENVY_CACHE_ROOT"] = str(self._cache_dir)
        test_config.run(
            [str(self._envy), "init", str(project_dir), str(bin_dir)],
            capture_output=True,
            text=True,
            env=env,
            timeout=30,
        )

        self._hook_path = self._cache_dir / "shell" / "hook.bash"
        assert self._hook_path.exists(), f"Hook not found at {self._hook_path}"

    def tearDown(self) -> None:
        if hasattr(self, "_temp_dir") and self._temp_dir.exists():
            shutil.rmtree(self._temp_dir, ignore_errors=True)

    def _run_bash_hook_test(self, script: str) -> subprocess.CompletedProcess[str]:
        """Run a bash script that sources the hook and tests behavior."""
        return test_config.run(
            ["bash", "-e", "-c", script],
            capture_output=True,
            text=True,
            timeout=10,
        )

    def _make_envy_project(self, name: str, bin_val: str = "tools") -> Path:
        """Create a minimal envy project directory."""
        project = self._temp_dir / name
        project.mkdir(parents=True, exist_ok=True)
        (project / "tools").mkdir(exist_ok=True)
        manifest = project / "envy.lua"
        manifest.write_text(
            f'-- @envy bin "{bin_val}"\n-- @envy version "0.0.0"\nPACKAGES = {{}}\n'
        )
        return project

    def test_cd_into_project_adds_bin_to_path(self) -> None:
        project = self._make_envy_project("proj1")
        result = self._run_bash_hook_test(
            f'source "{self._hook_path}"\n'
            f'cd "{project}"\n'
            f'_ENVY_LAST_PWD=""\n'  # force re-check
            f"_envy_hook\n"
            f'echo "$PATH"'
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertIn(str(project / "tools"), result.stdout)

    def test_cd_into_subdirectory_keeps_path(self) -> None:
        project = self._make_envy_project("proj2")
        sub = project / "src"
        sub.mkdir()
        result = self._run_bash_hook_test(
            f'source "{self._hook_path}"\n'
            f'cd "{project}"\n'
            f'_ENVY_LAST_PWD=""\n'
            f"_envy_hook\n"
            f'cd "{sub}"\n'
            f'_ENVY_LAST_PWD=""\n'
            f"_envy_hook\n"
            f'echo "$PATH"'
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertIn(str(project / "tools"), result.stdout)

    def test_cd_out_removes_bin_from_path(self) -> None:
        project = self._make_envy_project("proj3")
        result = self._run_bash_hook_test(
            f'source "{self._hook_path}"\n'
            f'cd "{project}"\n'
            f'_ENVY_LAST_PWD=""\n'
            f"_envy_hook\n"
            f"cd /tmp\n"
            f'_ENVY_LAST_PWD=""\n'
            f"_envy_hook\n"
            f'echo "$PATH"'
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertNotIn(str(project / "tools"), result.stdout)

    def test_cd_between_projects_swaps_path(self) -> None:
        proj_a = self._make_envy_project("projA", "tools")
        proj_b = self._make_envy_project("projB", "tools")
        result = self._run_bash_hook_test(
            f'source "{self._hook_path}"\n'
            f'cd "{proj_a}"\n'
            f'_ENVY_LAST_PWD=""\n'
            f"_envy_hook\n"
            f'cd "{proj_b}"\n'
            f'_ENVY_LAST_PWD=""\n'
            f"_envy_hook\n"
            f'echo "$PATH"'
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertIn(str(proj_b / "tools"), result.stdout)
        self.assertNotIn(str(proj_a / "tools"), result.stdout)

    def test_envy_project_root_set(self) -> None:
        project = self._make_envy_project("proj4")
        result = self._run_bash_hook_test(
            f'source "{self._hook_path}"\n'
            f'cd "{project}"\n'
            f'_ENVY_LAST_PWD=""\n'
            f"_envy_hook\n"
            f'echo "$ENVY_PROJECT_ROOT"'
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertEqual(str(project), result.stdout.strip())

    def test_envy_project_root_unset_when_leaving(self) -> None:
        project = self._make_envy_project("proj5")
        result = self._run_bash_hook_test(
            f'source "{self._hook_path}"\n'
            f'cd "{project}"\n'
            f'_ENVY_LAST_PWD=""\n'
            f"_envy_hook\n"
            f"cd /tmp\n"
            f'_ENVY_LAST_PWD=""\n'
            f"_envy_hook\n"
            f'echo "root=${{ENVY_PROJECT_ROOT:-unset}}"'
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertEqual("root=unset", result.stdout.strip())

    def test_missing_bin_directive_no_path_change(self) -> None:
        project = self._temp_dir / "no-bin"
        project.mkdir(parents=True)
        (project / "envy.lua").write_text('-- @envy version "0.0.0"\nPACKAGES = {}\n')
        original_path = "/usr/bin:/bin"
        result = self._run_bash_hook_test(
            f'export PATH="{original_path}"\n'
            f'source "{self._hook_path}"\n'
            f'cd "{project}"\n'
            f'_ENVY_LAST_PWD=""\n'
            f"_envy_hook\n"
            f'echo "$PATH"'
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertEqual(original_path, result.stdout.strip())

    def test_disable_env_var(self) -> None:
        project = self._make_envy_project("proj-disable")
        result = self._run_bash_hook_test(
            f"export ENVY_SHELL_HOOK_DISABLE=1\n"
            f'export PATH="/usr/bin:/bin"\n'
            f'source "{self._hook_path}"\n'
            f'cd "{project}"\n'
            f'_ENVY_LAST_PWD=""\n'
            f"_envy_hook\n"
            f'echo "$PATH"'
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertNotIn(str(project / "tools"), result.stdout)

    def test_nested_root_false_finds_ancestor(self) -> None:
        parent = self._make_envy_project("parent-proj")
        child = parent / "sub"
        child.mkdir(parents=True, exist_ok=True)
        (child / "tools").mkdir(exist_ok=True)
        (child / "envy.lua").write_text(
            '-- @envy root "false"\n'
            '-- @envy bin "tools"\n'
            '-- @envy version "0.0.0"\n'
            "PACKAGES = {}\n"
        )
        result = self._run_bash_hook_test(
            f'source "{self._hook_path}"\n'
            f'cd "{child}"\n'
            f'_ENVY_LAST_PWD=""\n'
            f"_envy_hook\n"
            f'echo "root=$ENVY_PROJECT_ROOT"'
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        # Should find the parent project (which is root=true by default)
        self.assertIn(str(parent), result.stdout)

    def test_bin_dir_with_spaces(self) -> None:
        project = self._temp_dir / "space proj"
        project.mkdir(parents=True, exist_ok=True)
        (project / "my tools").mkdir(exist_ok=True)
        (project / "envy.lua").write_text(
            '-- @envy bin "my tools"\n-- @envy version "0.0.0"\nPACKAGES = {}\n'
        )
        result = self._run_bash_hook_test(
            f'source "{self._hook_path}"\n'
            f'cd "{project}"\n'
            f'_ENVY_LAST_PWD=""\n'
            f"_envy_hook\n"
            f'echo "$PATH"'
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertIn(str(project / "my tools"), result.stdout)

    def test_existing_path_entries_preserved(self) -> None:
        project = self._make_envy_project("proj-path")
        result = self._run_bash_hook_test(
            f'export PATH="/usr/local/bin:/usr/bin:/bin"\n'
            f'source "{self._hook_path}"\n'
            f'cd "{project}"\n'
            f'_ENVY_LAST_PWD=""\n'
            f"_envy_hook\n"
            f'echo "$PATH"'
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertIn("/usr/local/bin", result.stdout)
        self.assertIn("/usr/bin", result.stdout)
        self.assertIn("/bin", result.stdout)
        self.assertIn(str(project / "tools"), result.stdout)

    def test_initial_activation_inside_project(self) -> None:
        """Sourcing hook while already in a project activates immediately."""
        project = self._make_envy_project("proj-init")
        result = self._run_bash_hook_test(
            f'cd "{project}"\nsource "{self._hook_path}"\necho "$PATH"'
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertIn(str(project / "tools"), result.stdout)

    # --- v2: enter/leave messages ---

    def test_entering_message_on_cd_into_project(self) -> None:
        project = self._make_envy_project("proj-msg-enter")
        result = self._run_bash_hook_test(
            f'source "{self._hook_path}"\n'
            f'cd "{project}"\n'
            f'_ENVY_LAST_PWD=""\n'
            f"_envy_hook\n"
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertIn("envy: entering proj-msg-enter", result.stderr)
        self.assertIn("tools added to PATH", result.stderr)

    def test_leaving_message_on_cd_out(self) -> None:
        project = self._make_envy_project("proj-msg-leave")
        result = self._run_bash_hook_test(
            f'source "{self._hook_path}"\n'
            f'cd "{project}"\n'
            f'_ENVY_LAST_PWD=""\n'
            f"_envy_hook\n"
            f"cd /tmp\n"
            f'_ENVY_LAST_PWD=""\n'
            f"_envy_hook\n"
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertIn("envy: leaving proj-msg-leave", result.stderr)
        self.assertIn("PATH restored", result.stderr)

    def test_switching_projects_prints_leave_and_enter(self) -> None:
        proj_a = self._make_envy_project("proj-sw-A")
        proj_b = self._make_envy_project("proj-sw-B")
        result = self._run_bash_hook_test(
            f'source "{self._hook_path}"\n'
            f'cd "{proj_a}"\n'
            f'_ENVY_LAST_PWD=""\n'
            f"_envy_hook\n"
            f'cd "{proj_b}"\n'
            f'_ENVY_LAST_PWD=""\n'
            f"_envy_hook\n"
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        leave_pos = result.stderr.index("leaving proj-sw-A")
        enter_pos = result.stderr.index("entering proj-sw-B")
        self.assertLess(leave_pos, enter_pos)

    def test_no_duplicate_message_within_project(self) -> None:
        project = self._make_envy_project("proj-nodup")
        sub = project / "src"
        sub.mkdir()
        result = self._run_bash_hook_test(
            f'source "{self._hook_path}"\n'
            f'cd "{project}"\n'
            f'_ENVY_LAST_PWD=""\n'
            f"_envy_hook\n"
            f'cd "{sub}"\n'
            f'_ENVY_LAST_PWD=""\n'
            f"_envy_hook\n"
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertEqual(1, result.stderr.count("entering proj-nodup"))

    def test_initial_activation_prints_message(self) -> None:
        project = self._make_envy_project("proj-init-msg")
        result = self._run_bash_hook_test(
            f'cd "{project}"\nsource "{self._hook_path}"\n'
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertIn("envy: entering proj-init-msg", result.stderr)

    # --- v2: raccoon prompt ---

    def test_raccoon_in_prompt_when_in_project(self) -> None:
        project = self._make_envy_project("proj-raccoon")
        result = self._run_bash_hook_test(
            f'PS1="$ "\n'
            f'source "{self._hook_path}"\n'
            f'cd "{project}"\n'
            f'_ENVY_LAST_PWD=""\n'
            f"_envy_hook\n"
            f'echo "$PS1"'
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertIn("\U0001f99d", result.stdout)

    def test_raccoon_removed_on_leave(self) -> None:
        project = self._make_envy_project("proj-raccoon-rm")
        result = self._run_bash_hook_test(
            f'PS1="$ "\n'
            f'source "{self._hook_path}"\n'
            f'cd "{project}"\n'
            f'_ENVY_LAST_PWD=""\n'
            f"_envy_hook\n"
            f"cd /tmp\n"
            f'_ENVY_LAST_PWD=""\n'
            f"_envy_hook\n"
            f'echo "$PS1"'
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertNotIn("\U0001f99d", result.stdout)
        self.assertIn("$", result.stdout)

    def test_raccoon_disabled_via_env_var(self) -> None:
        project = self._make_envy_project("proj-raccoon-off")
        result = self._run_bash_hook_test(
            f"export ENVY_NO_PROMPT=1\n"
            f'PS1="$ "\n'
            f'source "{self._hook_path}"\n'
            f'cd "{project}"\n'
            f'_ENVY_LAST_PWD=""\n'
            f"_envy_hook\n"
            f'echo "$PS1"'
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertNotIn("\U0001f99d", result.stdout)
        self.assertIn("$", result.stdout)

    def test_non_utf8_locale_no_raccoon_and_ascii_dash(self) -> None:
        project = self._make_envy_project("proj-c-locale")
        result = self._run_bash_hook_test(
            f"export LANG=C\n"
            f"unset LC_ALL LC_CTYPE\n"
            f'PS1="$ "\n'
            f'source "{self._hook_path}"\n'
            f'cd "{project}"\n'
            f'_ENVY_LAST_PWD=""\n'
            f"_envy_hook\n"
            f'echo "$PS1"'
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertNotIn("\U0001f99d", result.stdout)
        self.assertIn("--", result.stderr)
        self.assertNotIn("\u2014", result.stderr)


@unittest.skipUnless(shutil.which("zsh"), "zsh not installed")
@unittest.skipIf(sys.platform == "win32", "zsh hook tests require Unix")
class TestZshHook(unittest.TestCase):
    """Test zsh shell hook behavior."""

    def setUp(self) -> None:
        self._temp_dir = Path(tempfile.mkdtemp(prefix="envy-zsh-hook-test-"))
        self._cache_dir = self._temp_dir / "cache"
        self._envy = _get_envy_binary()

        project_dir = self._temp_dir / "setup-project"
        bin_dir = self._temp_dir / "setup-bin"
        env = test_config.get_test_env()
        env["ENVY_CACHE_ROOT"] = str(self._cache_dir)
        test_config.run(
            [str(self._envy), "init", str(project_dir), str(bin_dir)],
            capture_output=True,
            text=True,
            env=env,
            timeout=30,
        )
        self._hook_path = self._cache_dir / "shell" / "hook.zsh"

    def tearDown(self) -> None:
        if hasattr(self, "_temp_dir") and self._temp_dir.exists():
            shutil.rmtree(self._temp_dir, ignore_errors=True)

    def _make_envy_project(self, name: str) -> Path:
        project = self._temp_dir / name
        project.mkdir(parents=True, exist_ok=True)
        (project / "tools").mkdir(exist_ok=True)
        (project / "envy.lua").write_text(
            '-- @envy bin "tools"\n-- @envy version "0.0.0"\nPACKAGES = {}\n'
        )
        return project

    def _run_zsh_hook_test(self, script: str) -> subprocess.CompletedProcess[str]:
        """Run a zsh script with -f (skip RC files) to avoid CI hangs."""
        return test_config.run(
            ["zsh", "-f", "-c", script],
            capture_output=True,
            text=True,
            timeout=30,
        )

    def test_cd_into_project_adds_bin_to_path(self) -> None:
        project = self._make_envy_project("zsh-proj1")
        result = self._run_zsh_hook_test(
            f'source "{self._hook_path}"\ncd "{project}"\necho "$PATH"'
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertIn(str(project / "tools"), result.stdout)

    def test_cd_out_removes_path(self) -> None:
        project = self._make_envy_project("zsh-proj2")
        result = self._run_zsh_hook_test(
            f'source "{self._hook_path}"\ncd "{project}"\ncd /tmp\necho "$PATH"'
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertNotIn(str(project / "tools"), result.stdout)

    def test_cd_between_projects_swaps_path(self) -> None:
        proj_a = self._make_envy_project("zsh-projA")
        proj_b = self._make_envy_project("zsh-projB")
        result = self._run_zsh_hook_test(
            f'source "{self._hook_path}"\ncd "{proj_a}"\ncd "{proj_b}"\necho "$PATH"'
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertIn(str(proj_b / "tools"), result.stdout)
        self.assertNotIn(str(proj_a / "tools"), result.stdout)

    def test_envy_project_root_set(self) -> None:
        project = self._make_envy_project("zsh-proj-root")
        result = self._run_zsh_hook_test(
            f'source "{self._hook_path}"\ncd "{project}"\necho "$ENVY_PROJECT_ROOT"'
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertEqual(str(project), result.stdout.strip())

    # --- v2: enter/leave messages ---

    def test_entering_message_on_cd_into_project(self) -> None:
        project = self._make_envy_project("zsh-msg-enter")
        result = self._run_zsh_hook_test(
            f'source "{self._hook_path}"\ncd "{project}"\n'
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertIn("envy: entering zsh-msg-enter", result.stderr)
        self.assertIn("tools added to PATH", result.stderr)

    def test_leaving_message_on_cd_out(self) -> None:
        project = self._make_envy_project("zsh-msg-leave")
        result = self._run_zsh_hook_test(
            f'source "{self._hook_path}"\ncd "{project}"\ncd /tmp\n'
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertIn("envy: leaving zsh-msg-leave", result.stderr)
        self.assertIn("PATH restored", result.stderr)

    def test_switching_projects_prints_leave_and_enter(self) -> None:
        proj_a = self._make_envy_project("zsh-sw-A")
        proj_b = self._make_envy_project("zsh-sw-B")
        result = self._run_zsh_hook_test(
            f'source "{self._hook_path}"\ncd "{proj_a}"\ncd "{proj_b}"\n'
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        leave_pos = result.stderr.index("leaving zsh-sw-A")
        enter_pos = result.stderr.index("entering zsh-sw-B")
        self.assertLess(leave_pos, enter_pos)

    def test_no_duplicate_message_within_project(self) -> None:
        project = self._make_envy_project("zsh-nodup")
        sub = project / "src"
        sub.mkdir()
        result = self._run_zsh_hook_test(
            f'source "{self._hook_path}"\ncd "{project}"\ncd "{sub}"\n'
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertEqual(1, result.stderr.count("entering zsh-nodup"))

    def test_initial_activation_prints_message(self) -> None:
        project = self._make_envy_project("zsh-init-msg")
        result = self._run_zsh_hook_test(
            f'cd "{project}"\nsource "{self._hook_path}"\n'
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertIn("envy: entering zsh-init-msg", result.stderr)

    # --- v2: raccoon prompt ---

    def test_raccoon_in_prompt_when_in_project(self) -> None:
        project = self._make_envy_project("zsh-raccoon")
        result = self._run_zsh_hook_test(
            f'PROMPT="$ "\nsource "{self._hook_path}"\ncd "{project}"\necho "$PROMPT"'
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertIn("\U0001f99d", result.stdout)

    def test_raccoon_removed_on_leave(self) -> None:
        project = self._make_envy_project("zsh-raccoon-rm")
        result = self._run_zsh_hook_test(
            f'PROMPT="$ "\n'
            f'source "{self._hook_path}"\ncd "{project}"\ncd /tmp\n'
            f'echo "$PROMPT"'
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertNotIn("\U0001f99d", result.stdout)
        self.assertIn("$", result.stdout)

    def test_raccoon_disabled_via_env_var(self) -> None:
        project = self._make_envy_project("zsh-raccoon-off")
        result = self._run_zsh_hook_test(
            f"export ENVY_NO_PROMPT=1\n"
            f'PROMPT="$ "\n'
            f'source "{self._hook_path}"\ncd "{project}"\n'
            f'echo "$PROMPT"'
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertNotIn("\U0001f99d", result.stdout)
        self.assertIn("$", result.stdout)


@unittest.skipUnless(shutil.which("fish"), "fish not installed")
@unittest.skipIf(sys.platform == "win32", "fish hook tests require Unix")
class TestFishHook(unittest.TestCase):
    """Test fish shell hook behavior."""

    def setUp(self) -> None:
        self._temp_dir = Path(tempfile.mkdtemp(prefix="envy-fish-hook-test-"))
        self._cache_dir = self._temp_dir / "cache"
        self._envy = _get_envy_binary()

        project_dir = self._temp_dir / "setup-project"
        bin_dir = self._temp_dir / "setup-bin"
        env = test_config.get_test_env()
        env["ENVY_CACHE_ROOT"] = str(self._cache_dir)
        test_config.run(
            [str(self._envy), "init", str(project_dir), str(bin_dir)],
            capture_output=True,
            text=True,
            env=env,
            timeout=30,
        )
        self._hook_path = self._cache_dir / "shell" / "hook.fish"

    def tearDown(self) -> None:
        if hasattr(self, "_temp_dir") and self._temp_dir.exists():
            shutil.rmtree(self._temp_dir, ignore_errors=True)

    def _make_envy_project(self, name: str) -> Path:
        project = self._temp_dir / name
        project.mkdir(parents=True, exist_ok=True)
        (project / "tools").mkdir(exist_ok=True)
        (project / "envy.lua").write_text(
            '-- @envy bin "tools"\n-- @envy version "0.0.0"\nPACKAGES = {}\n'
        )
        return project

    def test_cd_into_project_adds_bin_to_path(self) -> None:
        project = self._make_envy_project("fish-proj1")
        result = test_config.run(
            [
                "fish",
                "-c",
                f'source "{self._hook_path}"\ncd "{project}"\necho $PATH',
            ],
            capture_output=True,
            text=True,
            timeout=30,
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertIn(str(project / "tools"), result.stdout)

    def test_cd_out_removes_path(self) -> None:
        project = self._make_envy_project("fish-proj2")
        result = test_config.run(
            [
                "fish",
                "-c",
                f'source "{self._hook_path}"\ncd "{project}"\ncd /tmp\necho $PATH',
            ],
            capture_output=True,
            text=True,
            timeout=30,
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertNotIn(str(project / "tools"), result.stdout)

    def test_cd_between_projects_swaps_path(self) -> None:
        proj_a = self._make_envy_project("fish-projA")
        proj_b = self._make_envy_project("fish-projB")
        result = test_config.run(
            [
                "fish",
                "-c",
                f'source "{self._hook_path}"\ncd "{proj_a}"\ncd "{proj_b}"\necho $PATH',
            ],
            capture_output=True,
            text=True,
            timeout=30,
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertIn(str(proj_b / "tools"), result.stdout)
        self.assertNotIn(str(proj_a / "tools"), result.stdout)

    def test_envy_project_root_set(self) -> None:
        project = self._make_envy_project("fish-proj-root")
        result = test_config.run(
            [
                "fish",
                "-c",
                f'source "{self._hook_path}"\ncd "{project}"\necho $ENVY_PROJECT_ROOT',
            ],
            capture_output=True,
            text=True,
            timeout=30,
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertEqual(str(project), result.stdout.strip())

    def _run_fish_hook_test(self, script: str) -> subprocess.CompletedProcess[str]:
        return test_config.run(
            ["fish", "-c", script],
            capture_output=True,
            text=True,
            timeout=30,
        )

    # --- v2: enter/leave messages ---

    def test_entering_message_on_cd_into_project(self) -> None:
        project = self._make_envy_project("fish-msg-enter")
        result = self._run_fish_hook_test(
            f'source "{self._hook_path}"\ncd "{project}"\n'
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertIn("envy: entering fish-msg-enter", result.stderr)
        self.assertIn("tools added to PATH", result.stderr)

    def test_leaving_message_on_cd_out(self) -> None:
        project = self._make_envy_project("fish-msg-leave")
        result = self._run_fish_hook_test(
            f'source "{self._hook_path}"\ncd "{project}"\ncd /tmp\n'
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertIn("envy: leaving fish-msg-leave", result.stderr)
        self.assertIn("PATH restored", result.stderr)

    def test_switching_projects_prints_leave_and_enter(self) -> None:
        proj_a = self._make_envy_project("fish-sw-A")
        proj_b = self._make_envy_project("fish-sw-B")
        result = self._run_fish_hook_test(
            f'source "{self._hook_path}"\ncd "{proj_a}"\ncd "{proj_b}"\n'
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        leave_pos = result.stderr.index("leaving fish-sw-A")
        enter_pos = result.stderr.index("entering fish-sw-B")
        self.assertLess(leave_pos, enter_pos)

    def test_no_duplicate_message_within_project(self) -> None:
        project = self._make_envy_project("fish-nodup")
        sub = project / "src"
        sub.mkdir()
        result = self._run_fish_hook_test(
            f'source "{self._hook_path}"\ncd "{project}"\ncd "{sub}"\n'
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertEqual(1, result.stderr.count("entering fish-nodup"))

    def test_initial_activation_prints_message(self) -> None:
        project = self._make_envy_project("fish-init-msg")
        result = self._run_fish_hook_test(
            f'cd "{project}"\nsource "{self._hook_path}"\n'
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertIn("envy: entering fish-init-msg", result.stderr)

    # --- v2: raccoon prompt ---

    def test_raccoon_in_prompt_when_in_project(self) -> None:
        project = self._make_envy_project("fish-raccoon")
        result = self._run_fish_hook_test(
            f'source "{self._hook_path}"\ncd "{project}"\nfish_prompt'
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertIn("\U0001f99d", result.stdout)

    def test_raccoon_removed_on_leave(self) -> None:
        project = self._make_envy_project("fish-raccoon-rm")
        result = self._run_fish_hook_test(
            f'function fish_prompt; echo "> "; end\n'
            f'source "{self._hook_path}"\ncd "{project}"\ncd /tmp\nfish_prompt'
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertNotIn("\U0001f99d", result.stdout)

    def test_raccoon_disabled_via_env_var(self) -> None:
        project = self._make_envy_project("fish-raccoon-off")
        result = self._run_fish_hook_test(
            f"set -gx ENVY_NO_PROMPT 1\n"
            f'source "{self._hook_path}"\ncd "{project}"\nfish_prompt'
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertNotIn("\U0001f99d", result.stdout)


@unittest.skipUnless(shutil.which("pwsh"), "pwsh not installed")
class TestPowerShellHook(unittest.TestCase):
    """Test PowerShell shell hook behavior."""

    def setUp(self) -> None:
        # .resolve() converts Windows 8.3 short names (RUNNER~1) to long names
        # (runneradmin) so Python paths match PowerShell's Resolve-Path output.
        self._temp_dir = Path(tempfile.mkdtemp(prefix="envy-pwsh-hook-test-")).resolve()
        self._cache_dir = self._temp_dir / "cache"
        self._envy = _get_envy_binary()

        project_dir = self._temp_dir / "setup-project"
        bin_dir = self._temp_dir / "setup-bin"
        env = test_config.get_test_env()
        env["ENVY_CACHE_ROOT"] = str(self._cache_dir)
        test_config.run(
            [str(self._envy), "init", str(project_dir), str(bin_dir)],
            capture_output=True,
            text=True,
            env=env,
            timeout=30,
        )
        self._hook_path = self._cache_dir / "shell" / "hook.ps1"
        assert self._hook_path.exists(), f"Hook not found at {self._hook_path}"

    def tearDown(self) -> None:
        if hasattr(self, "_temp_dir") and self._temp_dir.exists():
            shutil.rmtree(self._temp_dir, ignore_errors=True)

    def _run_pwsh_hook_test(self, script: str) -> subprocess.CompletedProcess[str]:
        """Run a PowerShell script that dot-sources the hook and tests behavior."""
        return test_config.run(
            ["pwsh", "-NoProfile", "-NonInteractive", "-Command", script],
            capture_output=True,
            text=True,
            timeout=30,
        )

    def _make_envy_project(self, name: str, bin_val: str = "tools") -> Path:
        """Create a minimal envy project directory."""
        project = self._temp_dir / name
        project.mkdir(parents=True, exist_ok=True)
        (project / "tools").mkdir(exist_ok=True)
        (project / "envy.lua").write_text(
            f'-- @envy bin "{bin_val}"\n-- @envy version "0.0.0"\nPACKAGES = {{}}\n'
        )
        return project

    def test_cd_into_project_adds_bin_to_path(self) -> None:
        project = self._make_envy_project("ps-proj1")
        result = self._run_pwsh_hook_test(
            f'. "{self._hook_path}"\n'
            f'Set-Location "{project}"\n'
            f"$global:_ENVY_LAST_PWD = $null\n"
            f"_envy_hook\n"
            f"Write-Output $env:PATH"
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertIn(str(project / "tools"), result.stdout)

    def test_cd_into_subdirectory_keeps_path(self) -> None:
        project = self._make_envy_project("ps-proj2")
        sub = project / "src"
        sub.mkdir()
        result = self._run_pwsh_hook_test(
            f'. "{self._hook_path}"\n'
            f'Set-Location "{project}"\n'
            f"$global:_ENVY_LAST_PWD = $null\n"
            f"_envy_hook\n"
            f'Set-Location "{sub}"\n'
            f"$global:_ENVY_LAST_PWD = $null\n"
            f"_envy_hook\n"
            f"Write-Output $env:PATH"
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertIn(str(project / "tools"), result.stdout)

    def test_cd_out_removes_bin_from_path(self) -> None:
        project = self._make_envy_project("ps-proj3")
        result = self._run_pwsh_hook_test(
            f'. "{self._hook_path}"\n'
            f'Set-Location "{project}"\n'
            f"$global:_ENVY_LAST_PWD = $null\n"
            f"_envy_hook\n"
            f'Set-Location "{self._temp_dir}"\n'
            f"$global:_ENVY_LAST_PWD = $null\n"
            f"_envy_hook\n"
            f"Write-Output $env:PATH"
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertNotIn(str(project / "tools"), result.stdout)

    def test_cd_between_projects_swaps_path(self) -> None:
        proj_a = self._make_envy_project("ps-projA")
        proj_b = self._make_envy_project("ps-projB")
        result = self._run_pwsh_hook_test(
            f'. "{self._hook_path}"\n'
            f'Set-Location "{proj_a}"\n'
            f"$global:_ENVY_LAST_PWD = $null\n"
            f"_envy_hook\n"
            f'Set-Location "{proj_b}"\n'
            f"$global:_ENVY_LAST_PWD = $null\n"
            f"_envy_hook\n"
            f"Write-Output $env:PATH"
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertIn(str(proj_b / "tools"), result.stdout)
        self.assertNotIn(str(proj_a / "tools"), result.stdout)

    def test_envy_project_root_set(self) -> None:
        project = self._make_envy_project("ps-proj4")
        result = self._run_pwsh_hook_test(
            f'. "{self._hook_path}"\n'
            f'Set-Location "{project}"\n'
            f"$global:_ENVY_LAST_PWD = $null\n"
            f"_envy_hook\n"
            f"Write-Output $env:ENVY_PROJECT_ROOT"
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertEqual(str(project), result.stdout.strip())

    def test_envy_project_root_unset_when_leaving(self) -> None:
        project = self._make_envy_project("ps-proj5")
        result = self._run_pwsh_hook_test(
            f'. "{self._hook_path}"\n'
            f'Set-Location "{project}"\n'
            f"$global:_ENVY_LAST_PWD = $null\n"
            f"_envy_hook\n"
            f'Set-Location "{self._temp_dir}"\n'
            f"$global:_ENVY_LAST_PWD = $null\n"
            f"_envy_hook\n"
            f'if ($env:ENVY_PROJECT_ROOT) {{ Write-Output "root=$env:ENVY_PROJECT_ROOT" }}'
            f' else {{ Write-Output "root=unset" }}'
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertEqual("root=unset", result.stdout.strip())

    def test_missing_bin_directive_no_path_change(self) -> None:
        project = self._temp_dir / "ps-no-bin"
        project.mkdir(parents=True)
        (project / "envy.lua").write_text('-- @envy version "0.0.0"\nPACKAGES = {}\n')
        sep = os.pathsep
        result = self._run_pwsh_hook_test(
            f'$env:PATH = "/usr/bin{sep}/bin"\n'
            f'. "{self._hook_path}"\n'
            f'Set-Location "{project}"\n'
            f"$global:_ENVY_LAST_PWD = $null\n"
            f"_envy_hook\n"
            f"Write-Output $env:PATH"
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertEqual(f"/usr/bin{sep}/bin", result.stdout.strip())

    def test_disable_env_var(self) -> None:
        project = self._make_envy_project("ps-proj-disable")
        sep = os.pathsep
        result = self._run_pwsh_hook_test(
            f'$env:ENVY_SHELL_HOOK_DISABLE = "1"\n'
            f'$env:PATH = "/usr/bin{sep}/bin"\n'
            f'. "{self._hook_path}"\n'
            f'Set-Location "{project}"\n'
            f"$global:_ENVY_LAST_PWD = $null\n"
            f"_envy_hook\n"
            f"Write-Output $env:PATH"
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertNotIn(str(project / "tools"), result.stdout)

    def test_nested_root_false_finds_ancestor(self) -> None:
        parent = self._make_envy_project("ps-parent-proj")
        child = parent / "sub"
        child.mkdir(parents=True, exist_ok=True)
        (child / "tools").mkdir(exist_ok=True)
        (child / "envy.lua").write_text(
            '-- @envy root "false"\n'
            '-- @envy bin "tools"\n'
            '-- @envy version "0.0.0"\n'
            "PACKAGES = {}\n"
        )
        result = self._run_pwsh_hook_test(
            f'. "{self._hook_path}"\n'
            f'Set-Location "{child}"\n'
            f"$global:_ENVY_LAST_PWD = $null\n"
            f"_envy_hook\n"
            f"Write-Output $env:ENVY_PROJECT_ROOT"
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertIn(str(parent), result.stdout)

    def test_initial_activation_inside_project(self) -> None:
        """Dot-sourcing hook while already in a project activates immediately."""
        project = self._make_envy_project("ps-proj-init")
        result = self._run_pwsh_hook_test(
            f'Set-Location "{project}"\n. "{self._hook_path}"\nWrite-Output $env:PATH'
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertIn(str(project / "tools"), result.stdout)

    # --- v2: enter/leave messages ---

    def test_entering_message_on_cd_into_project(self) -> None:
        project = self._make_envy_project("ps-msg-enter")
        result = self._run_pwsh_hook_test(
            f'. "{self._hook_path}"\n'
            f'Set-Location "{project}"\n'
            f"$global:_ENVY_LAST_PWD = $null\n"
            f"_envy_hook\n"
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        # PowerShell Write-Host goes to stdout in non-interactive mode
        combined = result.stdout + result.stderr
        self.assertIn("envy: entering ps-msg-enter", combined)
        self.assertIn("tools added to PATH", combined)

    def test_leaving_message_on_cd_out(self) -> None:
        project = self._make_envy_project("ps-msg-leave")
        result = self._run_pwsh_hook_test(
            f'. "{self._hook_path}"\n'
            f'Set-Location "{project}"\n'
            f"$global:_ENVY_LAST_PWD = $null\n"
            f"_envy_hook\n"
            f'Set-Location "{self._temp_dir}"\n'
            f"$global:_ENVY_LAST_PWD = $null\n"
            f"_envy_hook\n"
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        combined = result.stdout + result.stderr
        self.assertIn("envy: leaving ps-msg-leave", combined)
        self.assertIn("PATH restored", combined)

    def test_switching_projects_prints_leave_and_enter(self) -> None:
        proj_a = self._make_envy_project("ps-sw-A")
        proj_b = self._make_envy_project("ps-sw-B")
        result = self._run_pwsh_hook_test(
            f'. "{self._hook_path}"\n'
            f'Set-Location "{proj_a}"\n'
            f"$global:_ENVY_LAST_PWD = $null\n"
            f"_envy_hook\n"
            f'Set-Location "{proj_b}"\n'
            f"$global:_ENVY_LAST_PWD = $null\n"
            f"_envy_hook\n"
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        combined = result.stdout + result.stderr
        leave_pos = combined.index("leaving ps-sw-A")
        enter_pos = combined.index("entering ps-sw-B")
        self.assertLess(leave_pos, enter_pos)

    def test_no_duplicate_message_within_project(self) -> None:
        project = self._make_envy_project("ps-nodup")
        sub = project / "src"
        sub.mkdir()
        result = self._run_pwsh_hook_test(
            f'. "{self._hook_path}"\n'
            f'Set-Location "{project}"\n'
            f"$global:_ENVY_LAST_PWD = $null\n"
            f"_envy_hook\n"
            f'Set-Location "{sub}"\n'
            f"$global:_ENVY_LAST_PWD = $null\n"
            f"_envy_hook\n"
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        combined = result.stdout + result.stderr
        self.assertEqual(1, combined.count("entering ps-nodup"))

    def test_initial_activation_prints_message(self) -> None:
        project = self._make_envy_project("ps-init-msg")
        result = self._run_pwsh_hook_test(
            f'Set-Location "{project}"\n. "{self._hook_path}"\n'
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        combined = result.stdout + result.stderr
        self.assertIn("envy: entering ps-init-msg", combined)

    # --- v2: raccoon prompt ---

    def test_raccoon_in_prompt_when_in_project(self) -> None:
        project = self._make_envy_project("ps-raccoon")
        result = self._run_pwsh_hook_test(
            f'. "{self._hook_path}"\n'
            f'Set-Location "{project}"\n'
            f"$global:_ENVY_LAST_PWD = $null\n"
            f"_envy_hook\n"
            f"Write-Output $global:_ENVY_PROMPT_ACTIVE"
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertEqual("True", result.stdout.strip())

    def test_raccoon_removed_on_leave(self) -> None:
        project = self._make_envy_project("ps-raccoon-rm")
        result = self._run_pwsh_hook_test(
            f'. "{self._hook_path}"\n'
            f'Set-Location "{project}"\n'
            f"$global:_ENVY_LAST_PWD = $null\n"
            f"_envy_hook\n"
            f'Set-Location "{self._temp_dir}"\n'
            f"$global:_ENVY_LAST_PWD = $null\n"
            f"_envy_hook\n"
            f"Write-Output $global:_ENVY_PROMPT_ACTIVE"
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        self.assertEqual("False", result.stdout.strip())

    def test_raccoon_disabled_via_env_var(self) -> None:
        project = self._make_envy_project("ps-raccoon-off")
        result = self._run_pwsh_hook_test(
            f'$env:ENVY_NO_PROMPT = "1"\n'
            f'. "{self._hook_path}"\n'
            f'Set-Location "{project}"\n'
            f"$global:_ENVY_LAST_PWD = $null\n"
            f"_envy_hook\n"
            f"prompt"
        )
        self.assertEqual(0, result.returncode, f"stderr: {result.stderr}")
        combined = result.stdout + result.stderr
        self.assertNotIn("\U0001f99d", combined)


if __name__ == "__main__":
    unittest.main()

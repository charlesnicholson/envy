"""Functional tests for envy.git_resolve(repo, ref).

Resolves refs against a local ``file://`` repository built with the ``git``
binary, then compares envy's answer to ``git rev-parse``. No network. Requires
the ``git`` binary; the suite skips itself when it is absent.
"""

import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from . import test_config

_GIT = shutil.which("git")


@unittest.skipIf(_GIT is None, "git binary not available")
class TestGitResolve(unittest.TestCase):
    """Resolve tags/branches/shas of a local repo via the `envy lua` command."""

    def setUp(self) -> None:
        project_root = Path(__file__).resolve().parent.parent
        binary_name = "envy.exe" if sys.platform == "win32" else "envy"
        self._envy = project_root / "out" / "build" / binary_name
        self._project_root = project_root

        self._work = Path(tempfile.mkdtemp(prefix="envy-git-resolve-"))
        self._repo = self._work / "repo"
        self._repo.mkdir()
        self._build_repo()

    def tearDown(self) -> None:
        shutil.rmtree(self._work, ignore_errors=True)

    # -- repo construction ---------------------------------------------------

    def _git(self, *args: str) -> str:
        """Run git in the repo, returning stripped stdout. Deterministic identity."""
        result = subprocess.run(
            [
                _GIT,
                "-c",
                "user.name=envy-test",
                "-c",
                "user.email=envy@test.invalid",
                *args,
            ],
            cwd=self._repo,
            capture_output=True,
            text=True,
            check=True,
        )
        return result.stdout.strip()

    def _build_repo(self) -> None:
        # Two commits so tag/branch collisions can point at distinct oids.
        self._git("init", "-b", "main")
        (self._repo / "a.txt").write_text("a\n", encoding="utf-8")
        self._git("add", "a.txt")
        self._git("commit", "-m", "c1")
        self.c1 = self._git("rev-parse", "HEAD")

        self._git("tag", "v1.0")  # lightweight tag at c1
        self._git("tag", "-a", "v2.0", "-m", "annotated")  # annotated tag at c1

        (self._repo / "b.txt").write_text("b\n", encoding="utf-8")
        self._git("add", "b.txt")
        self._git("commit", "-m", "c2")
        self.c2 = self._git("rev-parse", "HEAD")  # main HEAD

        # Same short name "amb" on a branch (c2) and a tag (c1): ambiguous.
        self._git("branch", "amb")  # refs/heads/amb -> c2
        self._git("tag", "amb", self.c1)  # refs/tags/amb -> c1

        # Annotated tag object id (distinct from the commit it peels to).
        self.v2_tag_obj = self._git("rev-parse", "refs/tags/v2.0")
        self.assertNotEqual(self.v2_tag_obj, self.c1, "annotated tag must wrap a commit")

    # -- helpers -------------------------------------------------------------

    @property
    def _repo_url(self) -> str:
        return self._repo.as_uri()  # file:// URL, correct on every platform

    def _resolve(self, ref: str) -> subprocess.CompletedProcess:
        """Invoke `envy lua` on a script that prints git_resolve(repo, ref)."""
        script = self._work / "resolve.lua"
        # repo/ref are interpolated as Lua long-bracket strings -- no escaping.
        script.write_text(
            "envy.stdout(envy.git_resolve([==[{repo}]==], [==[{ref}]==]))\n".format(
                repo=self._repo_url, ref=ref
            ),
            encoding="utf-8",
        )
        env = os.environ.copy()
        env.setdefault("ENVY_CACHE_DIR", str(self._project_root / "out" / "cache"))
        return test_config.run(
            [str(self._envy), "lua", str(script)],
            capture_output=True,
            text=True,
            env=env,
        )

    def _resolve_ok(self, ref: str) -> str:
        result = self._resolve(ref)
        self.assertEqual(result.returncode, 0, f"stderr: {result.stderr}")
        self.assertEqual(result.stderr.strip(), "")
        return result.stdout.strip()

    # -- success cases -------------------------------------------------------

    def test_lightweight_tag_fully_qualified(self) -> None:
        self.assertEqual(self._resolve_ok("refs/tags/v1.0"), self.c1)

    def test_branch_fully_qualified(self) -> None:
        self.assertEqual(self._resolve_ok("refs/heads/main"), self.c2)

    def test_annotated_tag_peels_to_commit(self) -> None:
        # Must return the commit the tag points at, not the tag object itself.
        resolved = self._resolve_ok("refs/tags/v2.0")
        self.assertEqual(resolved, self.c1)
        self.assertNotEqual(resolved, self.v2_tag_obj)

    def test_bare_tag_suffix_match(self) -> None:
        self.assertEqual(self._resolve_ok("v1.0"), self.c1)

    def test_full_sha_uppercase_passthrough_lowercases(self) -> None:
        # Full sha is returned normalized to lowercase without contacting the repo.
        self.assertEqual(self._resolve_ok(self.c1.upper()), self.c1)

    # -- error cases ---------------------------------------------------------

    def test_missing_ref_fails(self) -> None:
        result = self._resolve("refs/tags/does-not-exist")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("not found", result.stderr.lower())

    def test_ambiguous_bare_ref_fails(self) -> None:
        result = self._resolve("amb")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("ambiguous", result.stderr.lower())


if __name__ == "__main__":
    unittest.main()

"""Tests for the functional-test harness itself (functional_tests/test_config.py).

The pwsh crash detector decides whether a failing pwsh run is a real test failure or CI
infrastructure noise. Getting it wrong is expensive in both directions: too narrow and the
suite goes red for reasons unrelated to envy, too broad and it silently swallows genuine
hook regressions. It cannot be exercised by the pwsh tests themselves — reproducing a
CoreCLR crash on demand is not possible — so it is pinned here instead.
"""

from __future__ import annotations

import subprocess
import sys
import unittest

from . import test_config


def _result(returncode: int, stderr: str = "") -> subprocess.CompletedProcess[str]:
    return subprocess.CompletedProcess(
        args=["pwsh"], returncode=returncode, stderr=stderr
    )


class TestPwshCrashDetection(unittest.TestCase):
    """is_pwsh_runtime_crash must separate runtime death from a script's verdict."""

    def test_success_is_never_a_crash(self) -> None:
        self.assertFalse(test_config.is_pwsh_runtime_crash(_result(0)))
        # Even if something crash-shaped shows up on stderr, rc==0 means the script ran.
        self.assertFalse(
            test_config.is_pwsh_runtime_crash(_result(0, "Stack overflow."))
        )

    def test_sigsegv_with_no_output_is_a_crash(self) -> None:
        """The regression this file exists for.

        24 pwsh tests failed on linux-arm64 asan with rc=-11 and empty stderr. The
        detector only matched stderr text, so the crash was reported as a genuine result
        and every assertion against it failed.
        """
        self.assertTrue(test_config.is_pwsh_runtime_crash(_result(-11)))

    def test_any_signal_death_is_a_crash(self) -> None:
        # SIGABRT is how the two documented CoreCLR aborts terminate; SIGKILL is the
        # OOM-killer on a constrained runner. Neither is a verdict about the hook.
        for rc in (-6, -9, -11, -15):
            with self.subTest(returncode=rc):
                self.assertTrue(test_config.is_pwsh_runtime_crash(_result(rc)))

    def test_documented_coreclr_stderr_signatures_still_match(self) -> None:
        self.assertTrue(
            test_config.is_pwsh_runtime_crash(
                _result(1, "Unhandled exception. FileLoadException: bad assembly")
            )
        )
        self.assertTrue(test_config.is_pwsh_runtime_crash(_result(1, "Stack overflow.")))

    def test_genuine_script_failure_is_not_a_crash(self) -> None:
        """A real hook regression must stay visible.

        pwsh reports a script verdict through its own non-negative exit code, so these
        must pass straight through to the assertion in the test.
        """
        self.assertFalse(test_config.is_pwsh_runtime_crash(_result(1)))
        self.assertFalse(
            test_config.is_pwsh_runtime_crash(_result(1, "PATH did not contain envy-bin"))
        )
        # 139 is how a shell reports a *child's* SIGSEGV. pwsh surviving its child's crash
        # is not pwsh crashing, so it must not be retried away.
        self.assertFalse(test_config.is_pwsh_runtime_crash(_result(139)))

    def test_describe_exit_names_the_signal(self) -> None:
        # The blank retry message was why the original failure logged no reason at all.
        self.assertEqual("exit 1", test_config.describe_exit(_result(1)))
        self.assertIn("SIGSEGV", test_config.describe_exit(_result(-11)))


@unittest.skipIf(sys.platform == "win32", "no signals on Windows")
class TestPwshCrashRetry(unittest.TestCase):
    """run_pwsh must retry a segfaulting pwsh, and skip rather than fail if it never
    recovers."""

    # The stubs kill themselves with SIGKILL rather than the SIGSEGV that CoreCLR
    # actually raises. is_pwsh_runtime_crash treats every negative returncode
    # identically, and the SIGSEGV-specific coverage lives in the pure-function
    # tests above, which need no subprocess. Raising a real SIGSEGV here would
    # make macOS write a crash report and pop a "python quit unexpectedly" dialog
    # on every run.
    CRASH = "import os, signal; os.kill(os.getpid(), signal.SIGKILL)"

    def _run_stub(self, script_body: str, **kwargs):
        """Drive run_pwsh against a python stub standing in for pwsh."""
        return test_config.run_pwsh(
            [sys.executable, "-c", script_body],
            delay=0.0,
            capture_output=True,
            text=True,
            **kwargs,
        )

    def test_always_crashing_pwsh_skips_after_retries(self) -> None:
        with self.assertRaises(unittest.SkipTest) as caught:
            self._run_stub(self.CRASH)
        # The skip reason must name the signal, or CI shows a bare "skipped".
        self.assertIn("SIGKILL", str(caught.exception))

    def test_recovering_pwsh_returns_the_successful_attempt(self) -> None:
        """A crash on the first attempt must not fail the test if a retry succeeds."""
        import tempfile
        from pathlib import Path

        with tempfile.TemporaryDirectory() as tmp:
            marker = Path(tmp) / "attempted"
            body = (
                "import os, signal, pathlib, sys; "
                f"p = pathlib.Path({str(marker)!r}); "
                "first = not p.exists(); p.touch(); "
                "sys.stdout.write('recovered') if not first else "
                "os.kill(os.getpid(), signal.SIGKILL)"
            )
            result = self._run_stub(body)

        self.assertEqual(0, result.returncode)
        self.assertEqual("recovered", result.stdout)

    def test_genuine_failure_is_returned_not_retried_away(self) -> None:
        result = self._run_stub(
            "import sys; sys.stderr.write('real failure'); sys.exit(1)"
        )
        self.assertEqual(1, result.returncode)
        self.assertIn("real failure", result.stderr)


if __name__ == "__main__":
    unittest.main()

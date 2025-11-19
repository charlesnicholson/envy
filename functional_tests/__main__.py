from __future__ import annotations

import os
import pathlib
import sys
import time
import unittest
from concurrent.futures import ThreadPoolExecutor, as_completed

from . import test_config


def _build_default_argv() -> list[str]:
    root = pathlib.Path(__file__).resolve().parent
    return [
        "functional_tests",
        "discover",
        "-s",
        str(root),
        "-t",
        str(root.parent),
    ]


def _flatten_suite(suite: unittest.TestSuite) -> list[unittest.TestCase]:
    """Recursively flatten a test suite into individual test cases."""
    tests = []
    for test in suite:
        if isinstance(test, unittest.TestSuite):
            tests.extend(_flatten_suite(test))
        else:
            tests.append(test)
    return tests


def _run_single_test(test: unittest.TestCase, sanitizer_variant: str = "", verbose: bool = False) -> unittest.TestResult:
    """Run a single test case and return its result."""
    if verbose:
        test_name = f"{test.__class__.__module__}.{test.__class__.__name__}.{test._testMethodName}"
        variant_suffix = f" [{sanitizer_variant}]" if sanitizer_variant else ""
        print(f"\nRunning: {test_name}{variant_suffix}", flush=True)

    # Set which sanitizer variant to use for this test
    if sanitizer_variant:
        test_config.set_sanitizer_variant(sanitizer_variant)

    suite = unittest.TestSuite([test])
    result = unittest.TestResult()
    suite.run(result)

    return result


def _run_parallel(loader: unittest.TestLoader, root: pathlib.Path, jobs: int, verbose: bool = False) -> None:
    """Run tests in parallel using ThreadPoolExecutor."""
    start_time = time.time()

    # Discover all tests
    suite = loader.discover(str(root), top_level_dir=str(root.parent))
    base_test_cases = _flatten_suite(suite)

    if len(base_test_cases) == 0:
        print("No tests found")
        return

    # Discover available sanitizer variants from filesystem
    sanitizer_variants = test_config.get_sanitizer_variants()
    if not sanitizer_variants:
        print("ERROR: No functional tester executables found")
        sys.exit(1)

    # Create test cases for all available sanitizer variants
    # IMPORTANT: Create NEW test instances for each variant to avoid sharing state
    test_cases = []
    for test in base_test_cases:
        for variant in sanitizer_variants:
            # Create a fresh test instance for this variant
            test_class = test.__class__
            test_method = test._testMethodName
            new_test = test_class(test_method)
            test_cases.append((new_test, variant))

    total_tests = len(test_cases)
    variant_desc = f"{len(sanitizer_variants)} sanitizer variant{'s' if len(sanitizer_variants) > 1 else ''}"
    print(f"Running {total_tests} tests ({len(base_test_cases)} tests × {variant_desc}) with {jobs} workers..." + (" (verbose mode)" if verbose else ""))

    # Counters for results
    passed = 0
    failed = 0
    errors = 0
    skipped = 0
    failure_details = []

    # Run tests in parallel
    with ThreadPoolExecutor(max_workers=jobs) as executor:
        # Submit all tests
        future_to_test = {
            executor.submit(_run_single_test, test, variant, verbose): (test, variant)
            for test, variant in test_cases
        }

        # Collect results as they complete
        for i, future in enumerate(as_completed(future_to_test), 1):
            test, variant = future_to_test[future]
            try:
                result = future.result(timeout=60)
            except Exception as e:
                sys.stdout.write("E")
                errors += 1
                failure_details.append(("ERROR", f"{test} [{variant}]", f"Test execution failed: {e}\n"))
                sys.stdout.flush()
                continue

            # Categorize result
            if result.errors:
                sys.stdout.write("E")
                errors += 1
                for test_case, traceback in result.errors:
                    failure_details.append(("ERROR", test_case, traceback))
            elif result.failures:
                sys.stdout.write("F")
                failed += 1
                for test_case, traceback in result.failures:
                    failure_details.append(("FAIL", test_case, traceback))
            elif result.skipped:
                sys.stdout.write("s")
                skipped += 1
            else:
                sys.stdout.write(".")
                passed += 1

            sys.stdout.flush()

            # Line break every 70 chars
            if i % 70 == 0:
                sys.stdout.write("\n")

    sys.stdout.write("\n")

    # Print failure details
    if failure_details:
        sys.stdout.write("=" * 70 + "\n")
        for failure_type, test_case, traceback in failure_details:
            sys.stdout.write(f"{failure_type}: {test_case}\n")
            sys.stdout.write("-" * 70 + "\n")
            sys.stdout.write(traceback)
            sys.stdout.write("\n")

    # Print summary
    elapsed = time.time() - start_time
    sys.stdout.write("-" * 70 + "\n")
    sys.stdout.write(f"Ran {total_tests} tests in {elapsed:.3f}s\n")
    sys.stdout.write("\n")

    if failed + errors > 0:
        parts = []
        if failed:
            parts.append(f"failures={failed}")
        if errors:
            parts.append(f"errors={errors}")
        if skipped:
            parts.append(f"skipped={skipped}")
        sys.stdout.write(f"FAILED ({', '.join(parts)})\n")
        sys.exit(1)
    else:
        if skipped:
            sys.stdout.write(f"OK (skipped={skipped})\n")
        else:
            sys.stdout.write("OK\n")


def main() -> None:
    # Set up sanitizer options for the test process
    root = pathlib.Path(__file__).parent.parent
    tsan_supp = root / "tsan.supp"
    if tsan_supp.exists():
        os.environ.setdefault('TSAN_OPTIONS', f'suppressions={tsan_supp}')

    # Determine number of jobs for parallel execution
    jobs_env = os.environ.get("ENVY_TEST_JOBS")
    verbose = os.environ.get("ENVY_TEST_VERBOSE", "").lower() in ("1", "true", "yes")

    if jobs_env:
        # Explicit override via environment variable
        if jobs_env.lower() == "sequential" or jobs_env == "0":
            # Force sequential
            jobs = None
        else:
            try:
                jobs = int(jobs_env)
                if jobs < 1:
                    print(
                        f"Warning: Invalid ENVY_TEST_JOBS={jobs_env} (must be >= 1), using auto"
                    )
                    jobs = os.cpu_count() or 4
            except ValueError:
                print(
                    f"Warning: Invalid ENVY_TEST_JOBS={jobs_env} (not a number), using auto"
                )
                jobs = os.cpu_count() or 4
    else:
        # Auto-detect CPU count for parallel execution (default)
        jobs = os.cpu_count() or 4

    if jobs and jobs > 1:
        # Parallel execution
        root = pathlib.Path(__file__).resolve().parent
        loader = unittest.TestLoader()
        _run_parallel(loader, root, jobs, verbose)
        return

    # Sequential execution (explicit or fallback)
    argv = sys.argv if len(sys.argv) > 1 else _build_default_argv()
    if verbose:
        argv.append("-v")
    unittest.main(module=None, argv=argv)


if __name__ == "__main__":
    main()

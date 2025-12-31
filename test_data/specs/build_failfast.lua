-- Test build phase: multi-line BUILD string fails on first error (fail-fast)
-- This verifies that shell scripts stop execution when a command fails.
IDENTITY = "local.build_failfast@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = {strip = 1}

-- Multi-line shell script where second command fails.
-- The third command should NOT run due to fail-fast behavior.
BUILD = [[
echo "line1"
false
echo "line2_should_not_run" > failfast_marker.txt
]]

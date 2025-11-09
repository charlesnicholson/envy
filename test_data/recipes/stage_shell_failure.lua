-- Test shell script failure in stage phase
identity = "local.stage_shell_failure@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

-- Shell script that intentionally fails
stage = [[
  echo "About to fail"
  false  # This should cause stage to fail with strict mode enabled
  echo "This line should never execute"
]]

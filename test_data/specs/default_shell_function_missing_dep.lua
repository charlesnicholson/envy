-- Test: default_shell function calls envy.package(), spec missing dep → fails with error
IDENTITY = "local.default_shell_function_missing_dep@v1"

-- Intentionally NOT declaring dependency on default_shell_dep_tool

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  if envy.PLATFORM == "windows" then
    error("default_shell_function_missing_dep test only runs on POSIX")
  end

  envy.extract_all(fetch_dir, stage_dir, {strip = 1})

  -- This should FAIL because default_shell function calls envy.package("local.default_shell_dep_tool@v1")
  -- but we didn't declare it in dependencies
  envy.run([[
    set -eu
    printf "should_not_reach_here\n" > should_not_exist.txt
  ]])
end

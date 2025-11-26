-- Test: default_shell function calls ctx.asset(), recipe missing dep → fails with error
identity = "local.default_shell_function_missing_dep@v1"

-- Intentionally NOT declaring dependency on default_shell_dep_tool

fetch = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx, opts)
  if ENVY_PLATFORM == "windows" then
    error("default_shell_function_missing_dep test only runs on POSIX")
  end

  ctx.extract_all({strip = 1})

  -- This should FAIL because default_shell function calls ctx.asset("local.default_shell_dep_tool@v1")
  -- but we didn't declare it in dependencies
  ctx.run([[
    set -eu
    printf "should_not_reach_here\n" > should_not_exist.txt
  ]])
end

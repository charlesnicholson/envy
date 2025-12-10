-- Test: default_shell function calls ctx.asset(), recipe declares dep → succeeds
IDENTITY = "local.default_shell_function_with_dep@v1"

DEPENDENCIES = {
  "local.default_shell_dep_tool@v1"
}

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(ctx, opts)
  if ENVY_PLATFORM == "windows" then
    error("default_shell_function_with_dep test only runs on POSIX")
  end

  ctx.extract_all({strip = 1})

  -- This should use shell returned by manifest default_shell function
  -- The function calls ctx.asset("local.default_shell_dep_tool@v1") which should succeed
  -- because we declared it in dependencies
  ctx.run([[
    set -eu
    printf "default_shell_with_dep_works\n" > dep_marker.txt
  ]])
end

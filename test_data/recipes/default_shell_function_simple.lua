-- Test: default_shell as function (no ctx.asset) works
identity = "local.default_shell_function_simple@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  if ENVY_PLATFORM == "windows" then
    error("default_shell_function_simple test only runs on POSIX")
  end

  ctx.extract_all({strip = 1})

  -- This should use shell returned by manifest default_shell function
  ctx.run([[
    set -eu
    printf "default_shell_function_works\n" > function_marker.txt
  ]])
end

-- Test: default_shell as ENVY_SHELL constant works
identity = "local.default_shell_constant@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})

  -- This should use ENVY_SHELL.SH from manifest default_shell
  ctx.run([[
    set -eu
    printf "default_shell_constant_works\n" > constant_marker.txt
  ]])
end

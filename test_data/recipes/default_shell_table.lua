-- Test: default_shell as custom table works
IDENTITY = "local.default_shell_table@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(ctx, opts)
  if ENVY_PLATFORM == "windows" then
    error("default_shell_table test only runs on POSIX")
  end

  ctx.extract_all({strip = 1})

  -- This should use custom shell from manifest default_shell
  ctx.run([[
    set -eu
    printf "default_shell_table_works\n" > table_marker.txt
  ]])
end

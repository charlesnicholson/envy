-- Test: default_shell as ENVY_SHELL constant works
IDENTITY = "local.default_shell_constant@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {strip = 1})

  -- This should use ENVY_SHELL.SH from manifest default_shell
  envy.run([[
    set -eu
    printf "default_shell_constant_works\n" > constant_marker.txt
  ]])
end

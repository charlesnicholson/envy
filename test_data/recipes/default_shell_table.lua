-- Test: default_shell as custom table works
IDENTITY = "local.default_shell_table@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  if envy.PLATFORM == "windows" then
    error("default_shell_table test only runs on POSIX")
  end

  envy.extract_all(fetch_dir, stage_dir, {strip = 1})

  -- This should use custom shell from manifest default_shell
  envy.run([[
    set -eu
    printf "default_shell_table_works\n" > table_marker.txt
  ]])
end

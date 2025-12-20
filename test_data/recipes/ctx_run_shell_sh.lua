-- Verify envy.run() with shell="sh" on POSIX hosts
IDENTITY = "local.ctx_run_shell_sh@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  if envy.PLATFORM == "windows" then
    error("ctx_run_shell_sh should not run on Windows")
  end

  envy.extract_all(fetch_dir, stage_dir, {strip = 1})

  envy.run([[\
    set -eu\
    printf "shell=sh\n" > shell_sh_marker.txt\
  ]], { shell = ENVY_SHELL.SH })
end

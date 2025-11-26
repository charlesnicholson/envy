-- Verify ctx.run() with shell="sh" on POSIX hosts
identity = "local.ctx_run_shell_sh@v1"

fetch = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx, opts)
  if ENVY_PLATFORM == "windows" then
    error("ctx_run_shell_sh should not run on Windows")
  end

  ctx.extract_all({strip = 1})

  ctx.run([[\
    set -eu\
    printf "shell=sh\n" > shell_sh_marker.txt\
  ]], { shell = ENVY_SHELL.SH })
end

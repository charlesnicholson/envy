-- Test ctx.run() strict mode catches failures
identity = "local.ctx_run_strict_mode@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})

  if ENVY_PLATFORM == "windows" then
    ctx.run([[
      cmd /c exit 1
      echo "This should not execute"
    ]], { shell = "powershell" })
  else
    ctx.run([[
      set -euo pipefail
      false
      echo "This should not execute"
    ]])
  end
end

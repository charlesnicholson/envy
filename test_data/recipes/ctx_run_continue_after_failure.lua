-- Test ctx.run() continues after a failing command
identity = "local.ctx_run_continue_after_failure@v1"

fetch = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx, opts)
  ctx.extract_all({strip = 1})

  -- Script should keep running even if an intermediate command fails
  if ENVY_PLATFORM == "windows" then
    ctx.run([[
      cmd /c exit 1
      Set-Content -Path continued.txt -Value "This executes even after false"
    ]], {shell = ENVY_SHELL.POWERSHELL})
  else
    ctx.run([[
      false || true
      echo "This executes even after false" > continued.txt
    ]])
  end
end

-- Test basic ctx.run() execution
identity = "local.ctx_run_basic@v1"

fetch = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})

  if ENVY_PLATFORM == "windows" then
    ctx.run([[
      Set-Content -Path run_marker.txt -Value "Hello from ctx.run"
      Add-Content -Path run_marker.txt -Value ("Stage directory: " + (Get-Location).Path)
      exit 0
    ]], { shell = ENVY_SHELL.POWERSHELL })
  else
    ctx.run([[
      echo "Hello from ctx.run" > run_marker.txt
      echo "Stage directory: $(pwd)" >> run_marker.txt
    ]])
  end
end

-- Test ctx.run() with disable_strict allows failures
identity = "local.ctx_run_disable_strict@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})

  -- With disable_strict, this should continue
  if ENVY_PLATFORM == "windows" then
    ctx.run([[
      cmd /c exit 1
      Set-Content -Path continued.txt -Value "This executes even after false"
    ]], {disable_strict = true, shell = "powershell"})
  else
    ctx.run([[
      false
      echo "This executes even after false" > continued.txt
    ]], {disable_strict = true})
  end
end

-- Test ctx.run() with absolute cwd path
identity = "local.ctx_run_cwd_absolute@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})

  if ENVY_PLATFORM == "windows" then
    local temp = os.getenv("TEMP") or "C:\\\\Temp"
    local target = temp .. "\\\\envy_ctx_run_test.txt"
    target = string.gsub(target, "\\\\", "\\\\\\\\")
    ctx.run(string.format([[
      Set-Content -Path pwd_absolute.txt -Value (Get-Location).Path
      Set-Content -Path "%s" -Value "Running in TEMP"
    ]], target), {cwd = temp, shell = "powershell"})
  else
    ctx.run([[
      pwd > pwd_absolute.txt
      echo "Running in /tmp" > /tmp/envy_ctx_run_test.txt
    ]], {cwd = "/tmp"})
  end
end

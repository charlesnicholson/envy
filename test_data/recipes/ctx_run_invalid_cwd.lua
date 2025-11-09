-- Test ctx.run() error when cwd doesn't exist
identity = "local.ctx_run_invalid_cwd@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})

  local invalid_cwd
  local script
  if ENVY_PLATFORM == "windows" then
    invalid_cwd = "Z:/nonexistent/directory/that/does/not/exist"
    script = [[
      Write-Output "Should not execute"
    ]]
    ctx.run(script, {cwd = invalid_cwd, shell = "powershell"})
  else
    invalid_cwd = "/nonexistent/directory/that/does/not/exist"
    ctx.run([[
      echo "Should not execute"
    ]], {cwd = invalid_cwd})
  end
end

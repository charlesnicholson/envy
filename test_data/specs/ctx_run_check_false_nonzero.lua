-- Test envy.run() with check=false allows non-zero exit codes
IDENTITY = "local.ctx_run_check_false_nonzero@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {strip = 1})

  local res
  if envy.PLATFORM == "windows" then
    res = envy.run([[
      Write-Output "Command that fails"
      exit 7
    ]], { shell = ENVY_SHELL.POWERSHELL, check = false })
  else
    res = envy.run([[
      echo "Command that fails"
      exit 7
    ]], { check = false })
  end

  if res.exit_code ~= 7 then
    error("Expected exit_code to be 7, got " .. tostring(res.exit_code))
  end

  if envy.PLATFORM == "windows" then
    envy.run([[
      Set-Content -Path continued_after_failure.txt -Value "Recipe continued after non-zero exit"
    ]], { shell = ENVY_SHELL.POWERSHELL })
  else
    envy.run([[
      echo "Recipe continued after non-zero exit" > continued_after_failure.txt
    ]])
  end
end

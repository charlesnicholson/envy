-- Test ctx.run() check mode catches undefined variables
identity = "local.ctx_run_check_undefined@v1"

fetch = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx, opts)
  ctx.extract_all({strip = 1})

  if ENVY_PLATFORM == "windows" then
    ctx.run([[
      $ErrorActionPreference = "Stop"
      Write-Output "About to use undefined variable"
      if (-not $env:UNDEFINED_VARIABLE_XYZ) { throw "Undefined variable" }
      Write-Output "Value: $env:UNDEFINED_VARIABLE_XYZ"
      Set-Content -Path should_not_exist.txt -Value "Should not reach here"
    ]], { shell = ENVY_SHELL.POWERSHELL, check = true })
  else
    ctx.run([[
      set -euo pipefail
      echo "About to use undefined variable"
      echo "Value: $UNDEFINED_VARIABLE_XYZ"
      echo "Should not reach here" > should_not_exist.txt
    ]], { check = true })
  end
end

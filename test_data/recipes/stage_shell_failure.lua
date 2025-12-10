-- Test shell script failure in stage phase
IDENTITY = "local.stage_shell_failure@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

-- Shell script that intentionally fails
STAGE = function(ctx, opts)
  if ENVY_PLATFORM == "windows" then
    ctx.run([[
      Write-Output "About to fail"
      exit 9
    ]], { shell = ENVY_SHELL.POWERSHELL, check = true })
  else
    ctx.run([[
      set -euo pipefail
      echo "About to fail"
      false
    ]], { check = true })
  end
end

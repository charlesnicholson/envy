-- Test ctx.run() inherits environment variables like PATH
IDENTITY = "local.ctx_run_env_inherit@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(ctx, opts)
  ctx.extract_all({strip = 1})

  if envy.PLATFORM == "windows" then
    ctx.run([[
      Set-Content -Path inherited_path.txt -Value ("PATH=" + $env:Path)
      $echoPath = (Get-Command echo.exe).Source
      Set-Content -Path which_echo.txt -Value $echoPath
      if ($env:Path -and $env:Path.Length -gt 0) {
        Set-Content -Path path_verification.txt -Value "PATH inherited"
      } else {
        exit 1
      }
    ]], { shell = ENVY_SHELL.POWERSHELL })
  else
    ctx.run([[
      echo "PATH=$PATH" > inherited_path.txt
      which echo > which_echo.txt
      test -n "$PATH" && echo "PATH inherited" > path_verification.txt
    ]])
  end
end

-- Test ctx.run() with parent directory (..) in cwd
identity = "local.ctx_run_cwd_parent@v1"

fetch = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx, opts)
  ctx.extract_all({strip = 1})

  if ENVY_PLATFORM == "windows" then
    ctx.run([[
      New-Item -ItemType Directory -Force -Path "deep/nested/dir" | Out-Null
    ]], { shell = ENVY_SHELL.POWERSHELL })

    ctx.run([[
      Set-Content -Path pwd_from_parent.txt -Value (Get-Location).Path
      Set-Content -Path parent_marker.txt -Value "Using parent dir"
    ]], {cwd = "deep/nested/..", shell = ENVY_SHELL.POWERSHELL})
  else
    ctx.run([[
      mkdir -p deep/nested/dir
    ]])

    ctx.run([[
      pwd > pwd_from_parent.txt
      echo "Using parent dir" > parent_marker.txt
    ]], {cwd = "deep/nested/.."})
  end
end

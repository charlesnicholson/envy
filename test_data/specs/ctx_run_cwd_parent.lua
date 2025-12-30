-- Test envy.run() with parent directory (..) in cwd
IDENTITY = "local.ctx_run_cwd_parent@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {strip = 1})

  if envy.PLATFORM == "windows" then
    envy.run([[
      New-Item -ItemType Directory -Force -Path "deep/nested/dir" | Out-Null
    ]], { shell = ENVY_SHELL.POWERSHELL })

    envy.run([[
      Set-Content -Path pwd_from_parent.txt -Value (Get-Location).Path
      Set-Content -Path parent_marker.txt -Value "Using parent dir"
    ]], {cwd = "deep/nested/..", shell = ENVY_SHELL.POWERSHELL})
  else
    envy.run([[
      mkdir -p deep/nested/dir
    ]])

    envy.run([[
      pwd > pwd_from_parent.txt
      echo "Using parent dir" > parent_marker.txt
    ]], {cwd = "deep/nested/.."})
  end
end

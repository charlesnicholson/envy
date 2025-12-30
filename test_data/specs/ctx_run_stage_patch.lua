-- Test envy.run() in stage for patching
IDENTITY = "local.ctx_run_stage_patch@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {strip = 1})

  if envy.PLATFORM == "windows" then
    envy.run([[
      Set-Content -Path patch_log.txt -Value "Patching file"
      Set-Content -Path temp.txt -Value "old content"
      (Get-Content temp.txt) -replace "old","new" | Set-Content -Path temp.txt
      Add-Content -Path patch_log.txt -Value "Patch applied"
    ]], { shell = ENVY_SHELL.POWERSHELL })
  else
    envy.run([[
      echo "Patching file" > patch_log.txt
      echo "old content" > temp.txt
      sed 's/old/new/g' temp.txt > temp.txt.patched
      mv temp.txt.patched temp.txt
      echo "Patch applied" >> patch_log.txt
    ]])
  end
end

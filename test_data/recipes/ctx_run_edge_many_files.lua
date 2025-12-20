-- Test envy.run() creating many files
IDENTITY = "local.ctx_run_edge_many_files@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {strip = 1})

  if envy.PLATFORM == "windows" then
    envy.run([[
      New-Item -ItemType Directory -Force -Path many_files | Out-Null
      foreach ($i in 1..50) {
        $path = "many_files/file_$i.txt"
        Set-Content -Path $path -Value ("File " + $i + " content")
      }
      Set-Content -Path many_files_marker.txt -Value "Created many files"
    ]], { shell = ENVY_SHELL.POWERSHELL })
  else
    envy.run([[
      mkdir -p many_files
      for i in {1..50}; do
        echo "File $i content" > "many_files/file_$i.txt"
      done
      echo "Created many files" > many_files_marker.txt
    ]])
  end
end

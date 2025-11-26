-- Test ctx.run() with nested operations and complex scripts
identity = "local.ctx_run_complex_nested@v1"

fetch = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx, opts)
  ctx.extract_all({strip = 1})

  if ENVY_PLATFORM == "windows" then
    ctx.run([[
      $level2 = @("level2a", "level2b")
      $level3 = @("level3a", "level3b")
      foreach ($l2 in $level2) {
        foreach ($l3 in $level3) {
          $path = Join-Path -Path "level1" -ChildPath (Join-Path $l2 $l3)
          New-Item -ItemType Directory -Force -Path $path | Out-Null
          Set-Content -Path (Join-Path $path "data.txt") -Value ("Content in " + $path)
        }
      }

      Get-ChildItem -Path level1 -Recurse -Filter data.txt | ForEach-Object {
        Add-Content -Path found_files.txt -Value ("Found in " + $_.Directory.Name)
      }

      $dirCount = ([System.IO.Directory]::GetDirectories("level1", "*", [System.IO.SearchOption]::AllDirectories).Count + 1)
      $fileCount = [System.IO.Directory]::GetFiles("level1", "*", [System.IO.SearchOption]::AllDirectories).Count
      Set-Content -Path summary.txt -Value ("Total directories: " + $dirCount)
      Add-Content -Path summary.txt -Value ("Total files: " + $fileCount)

      foreach ($dir in Get-ChildItem -Path level1 -Directory) {
        $count = (Get-ChildItem -Path $dir.FullName -Recurse -File).Count
        if ($count -gt 0) {
          Add-Content -Path dir_summary.txt -Value ("Directory " + $dir.FullName + " has " + $count + " files")
        }
      }
    ]], { shell = ENVY_SHELL.POWERSHELL })
  else
    ctx.run([[
      mkdir -p level1/{level2a,level2b}/{level3a,level3b}

      for l2 in level1/level2*; do
        for l3 in $l2/level3*; do
          echo "Content in $l3" > "$l3/data.txt"
        done
      done

      find level1 -type f -name "data.txt" | while read file; do
        dirname=$(dirname "$file")
        basename=$(basename "$dirname")
        echo "Found in $basename" >> found_files.txt
      done

      echo "Total directories: $(find level1 -type d | wc -l)" > summary.txt
      echo "Total files: $(find level1 -type f | wc -l)" >> summary.txt

      for dir in level1/level2*; do
        if [ -d "$dir" ]; then
          count=$(find "$dir" -type f | wc -l)
          if [ $count -gt 0 ]; then
            echo "Directory $dir has $count files" >> dir_summary.txt
          fi
        fi
      done
    ]])
  end
end

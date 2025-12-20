-- Test envy.run() with shell pipes and redirection
IDENTITY = "local.ctx_run_with_pipes@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {strip = 1})

  if envy.PLATFORM == "windows" then
    envy.run([[
$content = @("line3", "line1", "line2")
$content | Sort-Object | Set-Content -Path sorted.txt
Get-Content sorted.txt | Where-Object { $_ -match "line2" } | Set-Content -Path grepped.txt
Add-Content -Path grepped.txt -Value "Pipes work"
    ]], { shell = ENVY_SHELL.POWERSHELL })
  else
    envy.run([[
      echo -e "line3\nline1\nline2" | sort > sorted.txt
      cat sorted.txt | grep "line2" > grepped.txt
      echo "Pipes work" >> grepped.txt
    ]])
  end
end

-- Test basic shell script stage phase
IDENTITY = "local.stage_shell_basic@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

-- Shell script that extracts and creates a marker file
if envy.PLATFORM == "windows" then
  -- PowerShell variant
  STAGE = [[
    # PowerShell still invoked; use tar (Windows 10+ includes bsdtar) and Out-File
    tar -xzf ../fetch/test.tar.gz --strip-components=1
    "stage script executed" | Out-File -Encoding UTF8 STAGE_MARKER.txt
    Get-ChildItem -Force | Format-List > DIR_LIST.txt
    if (-not (Test-Path STAGE_MARKER.txt)) { exit 1 }
    exit 0
  ]]
else
  STAGE = [[
    # Extract the archive manually (should be in fetch_dir)
    tar -xzf ../fetch/test.tar.gz --strip-components=1

    # Create a marker file to prove shell ran
    echo "stage script executed" > STAGE_MARKER.txt

    # List files for debugging
    ls -la
  ]]
end

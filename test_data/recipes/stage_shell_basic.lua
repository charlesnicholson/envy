-- Test basic shell script stage phase
identity = "local.stage_shell_basic@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

-- Shell script that extracts and creates a marker file
stage = [[
  # Extract the archive manually (should be in fetch_dir)
  tar -xzf ../fetch/test.tar.gz --strip-components=1

  # Create a marker file to prove shell ran
  echo "stage script executed" > STAGE_MARKER.txt

  # List files for debugging
  ls -la
]]

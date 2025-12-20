-- Test complex shell script operations in stage phase
IDENTITY = "local.stage_shell_complex@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

-- Shell script with complex operations
if envy.PLATFORM == "windows" then
  STAGE = [[
    tar -xzf ../fetch/test.tar.gz --strip-components=1
    New-Item -ItemType Directory -Force -Path custom/bin | Out-Null
    New-Item -ItemType Directory -Force -Path custom/lib | Out-Null
    New-Item -ItemType Directory -Force -Path custom/share | Out-Null
    Move-Item file1.txt custom/bin/;
    Move-Item file2.txt custom/lib/;
    $fileCount = (Get-ChildItem -Recurse -File | Measure-Object).Count
    @(
      'Stage phase executed successfully'
      'Files reorganized into custom structure'
      ("Working directory: $(Get-Location)")
      ("File count: $fileCount")
    ) | Out-File -Encoding UTF8 custom/share/metadata.txt
    if (-not (Test-Path custom/bin/file1.txt)) { exit 1 }
    if (-not (Test-Path custom/lib/file2.txt)) { exit 1 }
    if (-not (Test-Path custom/share/metadata.txt)) { exit 1 }
    Write-Output "Stage complete: custom structure created"
    exit 0
  ]]
else
  STAGE = [[
    # Extract with strip
    tar -xzf ../fetch/test.tar.gz --strip-components=1

    # Create directory structure
    mkdir -p custom/bin custom/lib custom/share

    # Move files to custom locations
    mv file1.txt custom/bin/
    mv file2.txt custom/lib/

    # Create a file with metadata
    cat > custom/share/metadata.txt << EOF
Stage phase executed successfully
Files reorganized into custom structure
Working directory: $(pwd)
File count: $(find . -type f | wc -l)
EOF

    # Ensure everything is in place
    test -f custom/bin/file1.txt || exit 1
    test -f custom/lib/file2.txt || exit 1
    test -f custom/share/metadata.txt || exit 1

    echo "Stage complete: custom structure created"
  ]]
end

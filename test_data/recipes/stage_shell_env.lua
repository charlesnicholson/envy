-- Test shell script with environment variable access
identity = "local.stage_shell_env@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

-- Shell script that uses environment variables
if ENVY_PLATFORM == "windows" then
  stage = [[
    tar -xzf ../fetch/test.tar.gz --strip-components=1
    $pathStatus = if ($env:Path) { 'yes' } else { '' }
    $homeStatus = if ($env:UserProfile) { 'yes' } else { '' }
    $userStatus = if ($env:USERNAME) { 'yes' } else { '' }
    @(
      "PATH is available: $pathStatus"
      "HOME is available: $homeStatus"
      "USER is available: $userStatus"
      ("Shell: powershell")
    ) | Out-File -Encoding UTF8 env_info.txt
    if (-not (Test-Path env_info.txt)) { exit 1 }
    if (-not (Select-String -Path env_info.txt -Pattern "PATH is available: yes" -Quiet)) { exit 1 }
    Write-Output "Environment check complete"
    exit 0
  ]]
else
  stage = [[
    # Extract archive
    tar -xzf ../fetch/test.tar.gz --strip-components=1

    # Write environment information to file
    cat > env_info.txt << EOF
PATH is available: ${PATH:+yes}
HOME is available: ${HOME:+yes}
USER is available: ${USER:+yes}
Shell: $(basename $SHELL)
EOF

    # Verify the file was created
    test -f env_info.txt || exit 1
    grep -q "PATH is available: yes" env_info.txt || exit 1

    echo "Environment check complete"
  ]]
end

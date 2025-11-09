-- Test shell script with environment variable access
identity = "local.stage_shell_env@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

-- Shell script that uses environment variables
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

-- Test envy.run() with only whitespace and comments
IDENTITY = "local.ctx_run_edge_whitespace@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {strip = 1})

  if envy.PLATFORM == "windows" then
    envy.run([[
      # This is a comment

      # Another comment


    ]], { shell = ENVY_SHELL.POWERSHELL })

    envy.run([[
      Set-Content -Path after_whitespace.txt -Value "After whitespace script"
    ]], { shell = ENVY_SHELL.POWERSHELL })
  else
    envy.run([[
      # This is a comment

      # Another comment


    ]])

    envy.run([[
      echo "After whitespace script" > after_whitespace.txt
    ]])
  end
end

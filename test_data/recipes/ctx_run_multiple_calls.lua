-- Test multiple envy.run() calls in sequence
IDENTITY = "local.ctx_run_multiple_calls@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {strip = 1})

  if envy.PLATFORM == "windows" then
    envy.run([[
      Set-Content -Path call1.txt -Value "Call 1"
    ]], { shell = ENVY_SHELL.POWERSHELL })
    envy.run([[
      Set-Content -Path call2.txt -Value "Call 2"
    ]], { shell = ENVY_SHELL.POWERSHELL })
    envy.run([[
      Set-Content -Path call3.txt -Value "Call 3"
    ]], { shell = ENVY_SHELL.POWERSHELL })
    envy.run([[
      Get-Content call1.txt, call2.txt, call3.txt | Set-Content -Path all_calls.txt
    ]], { shell = ENVY_SHELL.POWERSHELL })
  else
    envy.run([[
      echo "Call 1" > call1.txt
    ]])
    envy.run([[
      echo "Call 2" > call2.txt
    ]])
    envy.run([[
      echo "Call 3" > call3.txt
    ]])
    envy.run([[
      cat call1.txt call2.txt call3.txt > all_calls.txt
    ]])
  end
end

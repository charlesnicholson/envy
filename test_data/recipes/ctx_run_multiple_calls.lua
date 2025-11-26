-- Test multiple ctx.run() calls in sequence
identity = "local.ctx_run_multiple_calls@v1"

fetch = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx, opts)
  ctx.extract_all({strip = 1})

  if ENVY_PLATFORM == "windows" then
    ctx.run([[
      Set-Content -Path call1.txt -Value "Call 1"
    ]], { shell = ENVY_SHELL.POWERSHELL })
    ctx.run([[
      Set-Content -Path call2.txt -Value "Call 2"
    ]], { shell = ENVY_SHELL.POWERSHELL })
    ctx.run([[
      Set-Content -Path call3.txt -Value "Call 3"
    ]], { shell = ENVY_SHELL.POWERSHELL })
    ctx.run([[
      Get-Content call1.txt, call2.txt, call3.txt | Set-Content -Path all_calls.txt
    ]], { shell = ENVY_SHELL.POWERSHELL })
  else
    ctx.run([[
      echo "Call 1" > call1.txt
    ]])
    ctx.run([[
      echo "Call 2" > call2.txt
    ]])
    ctx.run([[
      echo "Call 3" > call3.txt
    ]])
    ctx.run([[
      cat call1.txt call2.txt call3.txt > all_calls.txt
    ]])
  end
end

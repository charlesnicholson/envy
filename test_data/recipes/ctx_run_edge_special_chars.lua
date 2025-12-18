-- Test ctx.run() with special characters
IDENTITY = "local.ctx_run_edge_special_chars@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(ctx, opts)
  ctx.extract_all({strip = 1})

  if envy.PLATFORM == "windows" then
    ctx.run([[
      Set-Content -Path special_chars.txt -Value 'Special chars: !@#$%^&*()_+-=[]{}|;:'',.<>?/~`'
      Add-Content -Path special_chars.txt -Value 'Quotes: "double" ''single'''
      Add-Content -Path special_chars.txt -Value 'Backslash: \ and newline: (literal)'
      if (-not (Test-Path special_chars.txt)) { exit 1 }
      exit 0
    ]], { shell = ENVY_SHELL.POWERSHELL })
  else
    ctx.run([[
      echo "Special chars: !@#$%^&*()_+-=[]{}|;:',.<>?/~\`" > special_chars.txt
      echo "Quotes: \"double\" 'single'" >> special_chars.txt
      echo "Backslash: \\ and newline: (literal)" >> special_chars.txt
    ]])
  end
end

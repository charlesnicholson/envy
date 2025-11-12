-- Test ctx.run() mixed with ctx.template()
identity = "local.ctx_run_with_template@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})

  if ENVY_PLATFORM == "windows" then
    ctx.run([[
      Set-Content -Path greeting.tmpl -Value "Hello {{name}}, you are {{age}} years old"
    ]], { shell = ENVY_SHELL.POWERSHELL })

    ctx.run([[
      $content = Get-Content greeting.tmpl
      $content = $content -replace "{{name}}","Alice" -replace "{{age}}","30"
      Set-Content -Path greeting.txt -Value $content
    ]], { shell = ENVY_SHELL.POWERSHELL })

    ctx.run([[
      if ((Select-String -Path greeting.txt -Pattern "Alice")) { Set-Content -Path template_check.txt -Value "Alice" }
      if ((Select-String -Path greeting.txt -Pattern "30")) { Add-Content -Path template_check.txt -Value "30" }
    ]], { shell = ENVY_SHELL.POWERSHELL })
  else
    ctx.run([[
      echo "Hello {{name}}, you are {{age}} years old" > greeting.tmpl
    ]])

    ctx.run([[
      name="Alice"
      age="30"
      sed "s/{{name}}/$name/g; s/{{age}}/$age/g" greeting.tmpl > greeting.txt
    ]])

    ctx.run([[
      grep "Alice" greeting.txt > template_check.txt
      grep "30" greeting.txt >> template_check.txt
    ]])
  end
end

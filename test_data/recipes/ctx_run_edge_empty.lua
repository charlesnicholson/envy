-- Test ctx.run() with empty script
identity = "local.ctx_run_edge_empty@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})

  if ENVY_PLATFORM == "windows" then
    ctx.run([[]], { shell = "powershell" })
    ctx.run([[
      Set-Content -Path after_empty.txt -Value "After empty script"
    ]], { shell = "powershell" })
  else
    ctx.run([[]])
    ctx.run([[
      echo "After empty script" > after_empty.txt
    ]])
  end
end

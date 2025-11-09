-- Test ctx.run() captures stderr output
identity = "local.ctx_run_output_stderr@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})

  if ENVY_PLATFORM == "windows" then
    ctx.run([[
      [System.Console]::Error.WriteLine("Line 1 to stderr")
      [System.Console]::Error.WriteLine("Line 2 to stderr")
      Set-Content -Path stderr_marker.txt -Value "stderr test complete"
    ]], { shell = "powershell" })
  else
    ctx.run([[
      echo "Line 1 to stderr" >&2
      echo "Line 2 to stderr" >&2
      echo "stderr test complete" > stderr_marker.txt
    ]])
  end
end

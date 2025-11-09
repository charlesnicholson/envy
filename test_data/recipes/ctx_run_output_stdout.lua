-- Test ctx.run() captures stdout output
identity = "local.ctx_run_output_stdout@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})

  if ENVY_PLATFORM == "windows" then
    ctx.run([[
      Write-Output "Line 1 to stdout"
      Write-Output "Line 2 to stdout"
      Write-Output "Line 3 to stdout"
      Set-Content -Path stdout_marker.txt -Value "stdout test complete"
    ]], { shell = "powershell" })
  else
    ctx.run([[
      echo "Line 1 to stdout"
      echo "Line 2 to stdout"
      echo "Line 3 to stdout"
      echo "stdout test complete" > stdout_marker.txt
    ]])
  end
end

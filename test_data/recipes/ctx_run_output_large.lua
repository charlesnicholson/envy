-- Test ctx.run() with large output
identity = "local.ctx_run_output_large@v1"

fetch = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx, opts)
  ctx.extract_all({strip = 1})

  if ENVY_PLATFORM == "windows" then
    ctx.run([[
      for ($i = 1; $i -le 100; $i++) {
        Write-Output "Output line $i with some content to make it longer"
      }
      Set-Content -Path large_output_marker.txt -Value "Large output test complete"
    ]], { shell = ENVY_SHELL.POWERSHELL })
  else
    ctx.run([[
      for i in {1..100}; do
        echo "Output line $i with some content to make it longer"
      done
      echo "Large output test complete" > large_output_marker.txt
    ]])
  end
end

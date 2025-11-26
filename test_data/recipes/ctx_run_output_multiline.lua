-- Test ctx.run() with multi-line output
identity = "local.ctx_run_output_multiline@v1"

fetch = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx, opts)
  ctx.extract_all({strip = 1})

  if ENVY_PLATFORM == "windows" then
    ctx.run([[
$thisContent = @"
This is line 1
This is line 2
This is line 3

This is line 5 (after blank line)
	This line has a tab
    This line has spaces
"@
Set-Content -Path output.txt -Value $thisContent
Get-Content output.txt | ForEach-Object { Write-Output $_ }
Set-Content -Path multiline_marker.txt -Value "Multi-line test complete"
    ]], { shell = ENVY_SHELL.POWERSHELL })
  else
    ctx.run([[
cat > output.txt <<'MULTILINE'
This is line 1
This is line 2
This is line 3

This is line 5 (after blank line)
	This line has a tab
    This line has spaces
MULTILINE

    cat output.txt
    echo "Multi-line test complete" > multiline_marker.txt
    ]])
  end
end

-- Test build phase: verify ctx.run() captures stdout/stderr
identity = "local.build_output_capture@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = {strip = 1}

build = function(ctx)
  print("Testing output capture")

  -- Capture output from command
  local result = ctx.run([[
    echo "line1"
    echo "line2"
    echo "line3"
  ]])

  -- Verify stdout contains all lines
  if not result.stdout:match("line1") then
    error("Missing line1 in stdout")
  end
  if not result.stdout:match("line2") then
    error("Missing line2 in stdout")
  end
  if not result.stdout:match("line3") then
    error("Missing line3 in stdout")
  end

  -- Test with special characters
  result = ctx.run([[
    echo "Special: !@#$%^&*()"
    echo "Unicode: 你好世界"
    echo "Quotes: 'single' \"double\""
  ]])

  if not result.stdout:match("Special:") then
    error("Missing special characters in output")
  end

  print("Output capture works correctly")
end

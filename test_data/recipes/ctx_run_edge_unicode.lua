-- Test ctx.run() with Unicode characters
identity = "local.ctx_run_edge_unicode@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})

  -- Unicode characters in output
  ctx.run([[
    echo "Unicode: Hello 世界 🌍 café" > unicode.txt
    echo "More Unicode: Ω α β γ δ" >> unicode.txt
    echo "Emoji: 😀 🎉 🚀" >> unicode.txt
  ]])
end

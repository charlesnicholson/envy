-- Test ctx.run() with special characters
identity = "local.ctx_run_edge_special_chars@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})

  -- Special characters in output
  ctx.run([[
    echo "Special chars: !@#$%^&*()_+-=[]{}|;:',.<>?/~\`" > special_chars.txt
    echo "Quotes: \"double\" 'single'" >> special_chars.txt
    echo "Backslash: \\ and newline: (literal)" >> special_chars.txt
  ]])
end

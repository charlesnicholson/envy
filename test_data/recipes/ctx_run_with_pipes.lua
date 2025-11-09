-- Test ctx.run() with shell pipes and redirection
identity = "local.ctx_run_with_pipes@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})

  -- Use pipes and redirection
  ctx.run([[
    echo -e "line3\nline1\nline2" | sort > sorted.txt
    cat sorted.txt | grep "line2" > grepped.txt
    echo "Pipes work" >> grepped.txt
  ]])
end

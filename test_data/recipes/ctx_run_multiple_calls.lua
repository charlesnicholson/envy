-- Test multiple ctx.run() calls in sequence
identity = "local.ctx_run_multiple_calls@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})

  -- First ctx.run
  ctx.run([[
    echo "Call 1" > call1.txt
  ]])

  -- Second ctx.run
  ctx.run([[
    echo "Call 2" > call2.txt
  ]])

  -- Third ctx.run
  ctx.run([[
    echo "Call 3" > call3.txt
  ]])

  -- Fourth ctx.run to verify all
  ctx.run([[
    cat call1.txt call2.txt call3.txt > all_calls.txt
  ]])
end

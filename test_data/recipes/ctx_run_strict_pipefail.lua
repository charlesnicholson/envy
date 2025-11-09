-- Test ctx.run() strict mode catches pipe failures
identity = "local.ctx_run_strict_pipefail@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})

  -- Strict mode pipefail should catch failure in middle of pipe
  ctx.run([[
    set -euo pipefail
    echo "Start" | false | cat > should_fail.txt
  ]])
end

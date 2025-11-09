-- Test ctx.run() with deeply nested relative cwd
identity = "local.ctx_run_cwd_nested@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})

  -- Create deep nested directory
  ctx.run([[
    mkdir -p level1/level2/level3/level4
  ]])

  -- Run in deeply nested directory
  ctx.run([[
    pwd > pwd_nested.txt
    echo "Deep nesting works" > nested_marker.txt
  ]], {cwd = "level1/level2/level3/level4"})
end

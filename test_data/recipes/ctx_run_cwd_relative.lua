-- Test ctx.run() with relative cwd option
identity = "local.ctx_run_cwd_relative@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})

  -- Create subdirectory
  ctx.run([[
    mkdir -p custom/subdir
  ]])

  -- Run in subdirectory (relative to stage_dir)
  ctx.run([[
    pwd > pwd_output.txt
    echo "Running in subdir" > marker.txt
  ]], {cwd = "custom/subdir"})
end

-- Test ctx.run() with parent directory (..) in cwd
identity = "local.ctx_run_cwd_parent@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})

  -- Create a subdirectory
  ctx.run([[
    mkdir -p deep/nested/dir
  ]])

  -- Run from subdirectory, use .. to go back
  ctx.run([[
    pwd > pwd_from_parent.txt
    echo "Using parent dir" > parent_marker.txt
  ]], {cwd = "deep/nested/.."})
end

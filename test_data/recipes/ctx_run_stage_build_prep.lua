-- Test ctx.run() in stage for build preparation
identity = "local.ctx_run_stage_build_prep@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})

  -- Typical build preparation tasks
  ctx.run([[
    mkdir -p build
    cd build
    echo "# Build configuration" > config.txt
    echo "CFLAGS=-O2" >> config.txt
  ]])
end

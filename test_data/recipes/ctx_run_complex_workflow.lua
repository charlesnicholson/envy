-- Test ctx.run() with complex real-world workflow
identity = "local.ctx_run_complex_workflow@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})

  -- Configure
  ctx.run([[
    mkdir -p build src include
    echo "PROJECT=myapp" > config.mk
    echo "VERSION=1.0.0" >> config.mk
  ]])

  -- Generate source using shell
  ctx.run([[
    echo "#define VERSION \"1.0.0\"" > src/version.h
  ]])

  -- Build preparation
  ctx.run([[
    cd build
    echo "Configuring..." > config.log
    echo "CFLAGS=-O2 -Wall" > build.mk
  ]], {cwd = "."})

  -- Verification
  ctx.run([[
    test -f config.mk || exit 1
    test -d build || exit 1
    test -f build/build.mk || exit 1
    echo "Workflow complete" > workflow_complete.txt
  ]])
end

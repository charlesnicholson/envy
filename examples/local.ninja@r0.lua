IDENTITY = "local.ninja@r0"

DEPENDENCIES = { { product = "python3" } }

FETCH = {
  { source = "https://github.com/ninja-build/ninja.git", ref = "v1.13.1" },
  { source = "https://github.com/google/googletest.git", ref = "v1.16.0" }
}

BUILD = function(ctx, opts)
  local cmd = envy.template([[
{{python}} ./configure.py --bootstrap --gtest-source-dir={{googletest}}
./ninja all
./ninja_test
  ]], {
    python = ctx.product("python3"),
    googletest = ctx.stage_dir .. "/googletest.git"
  })

  ctx.run(cmd, { cwd = ctx.stage_dir .. "/ninja.git" })
end

INSTALL = function(ctx, opts)
  ctx.move(ctx.stage_dir .. "/ninja.git/ninja" .. ENVY_EXE_EXT, ctx.install_dir)
  ctx.mark_install_complete()
end

PRODUCTS = { ninja = "ninja" .. ENVY_EXE_EXT }

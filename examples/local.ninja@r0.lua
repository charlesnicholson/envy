identity = "local.ninja@r0"

dependencies = { { recipe = "local.python@r0", source = "local.python@r0.lua" } }

fetch = {
  { source = "https://github.com/ninja-build/ninja.git", ref = "v1.13.1" },
  { source = "https://github.com/google/googletest.git", ref = "v1.16.0" }
}

build = function(ctx, opts)
  local cmd = envy.template([[
{{python}} ./configure.py --bootstrap --gtest-source-dir={{googletest}}
./ninja all
./ninja_test
  ]], { python = ctx.asset("local.python@r0") .. "/install/bin/python",
        googletest = ctx.stage_dir .. "/googletest.git" })

  ctx.run(cmd, { cwd = ctx.stage_dir .. "/ninja.git" })
end

install = function(ctx, opts)
  ctx.move(ctx.stage_dir .. "/ninja.git/ninja", ctx.install_dir .. "/ninja")
  ctx.mark_install_complete()
end

products = { ninja = "ninja" }

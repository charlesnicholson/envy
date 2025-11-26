identity = "local.gn@r0"

dependencies = {
  { recipe = "local.python@r0", source = "local.python@r0.lua" },
  { recipe = "local.ninja@r0", source = "local.ninja@r0.lua" },
}

fetch = {
  source = "https://gn.googlesource.com/gn.git",
  ref = "748c9571f3d18820a7989a880d5ddf220e54af1b"
}

build = function(ctx, opts)
  local cmd = envy.template([[
{{python}} build/gen.py
{{ninja}} -C out
out/gn_unittests
]], { python = ctx.asset("local.python@r0") .. "/install/bin/python",
      ninja = ctx.asset("local.ninja@r0") .. "/ninja" })

  ctx.run(cmd, { cwd = ctx.stage_dir .. "/gn.git" })
end

install = function(ctx, opts)
  ctx.move(ctx.stage_dir .. "/gn.git/out/gn", ctx.install_dir .. "/gn")
  ctx.mark_install_complete()
end


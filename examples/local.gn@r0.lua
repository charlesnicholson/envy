identity = "local.gn@r0"

dependencies = {
  { product = "python3" },
  { product = "ninja" },
}

fetch = {
  source = "https://gn.googlesource.com/gn.git",
  ref = "748c9571f3d18820a7989a880d5ddf220e54af1b"
}

build = function(ctx, opts)
  local cmd = [[
{{python}} build/gen.py
{{ninja}} -C out
out/gn_unittests
]]

  ctx.run(envy.template(cmd,
      { python = ctx.product("python3"), ninja = ctx.product("ninja") }),
    { cwd = ctx.stage_dir .. "/gn.git" })
end

install = function(ctx, opts)
  ctx.move(ctx.stage_dir .. "/gn.git/out/gn", ctx.install_dir .. "/gn")
  ctx.mark_install_complete()
end

products = { gn = "gn" }

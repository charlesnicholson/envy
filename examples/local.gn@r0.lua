IDENTITY = "local.gn@r0"

DEPENDENCIES = {
  { product = "python3" },
  { product = "ninja" },
}

VALIDATE = function(opts)
  if opts.ref == nil then
    return "'ref' is a required option (GN doesn't tag, so use a git committish)"
  end
end

FETCH = function(ctx, opts)
  return {
    source = "https://gn.googlesource.com/gn.git",
    ref = opts.ref
  }
end

BUILD = function(ctx, opts)
  local cmd = [[
{{python}} build/gen.py
{{ninja}} -C out
]]

  if ENVY_PLATFORM ~= "windows" then
    cmd = cmd .. [[
out/gn_unittests
]]
  end

  ctx.run(envy.template(cmd,
      { python = ctx.product("python3"), ninja = ctx.product("ninja") }),
    { cwd = ctx.stage_dir .. "/gn.git", check = true, shell = ENVY_SHELL.cmd })
end

local ext = (ENVY_PLATFORM == "windows") and ".exe" or ""

INSTALL = function(ctx, opts)
  ctx.move(ctx.stage_dir .. "/gn.git/out/gn" .. ext, ctx.install_dir)
  ctx.mark_install_complete()
end

PRODUCTS = { gn = "gn" .. ext }


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

FETCH = function(tmp_dir, opts)
  return {
    source = "https://gn.googlesource.com/gn.git",
    ref = opts.ref
  }
end

BUILD = function(install_dir, stage_dir, fetch_dir, tmp_dir, opts)
  local cmd = [[
{{python}} build/gen.py
{{ninja}} -C out
]]

  if envy.PLATFORM ~= "windows" then
    cmd = cmd .. [[
out/gn_unittests
]]
  end

  local shell = envy.PLATFORM == "windows" and ENVY_SHELL.CMD or ENVY_SHELL.BASH
  envy.run(envy.template(cmd,
      { python = envy.product("python3"), ninja = envy.product("ninja") }),
    { cwd = stage_dir .. "gn.git", check = true, shell = shell })
end

INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir, opts)
  envy.move(stage_dir .. "gn.git/out/gn" .. envy.EXE_EXT, install_dir)
end

PRODUCTS = { gn = "gn" .. envy.EXE_EXT }

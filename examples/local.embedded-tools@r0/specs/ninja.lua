IDENTITY = "local.ninja@r0"

DEPENDENCIES = { { product = "python3" } }

VALIDATE = function(opts)
  if opts.version == nil then
    return "version option is required"
  end
end

FETCH = function(tmp_dir, opts)
  return {
    { source = "https://github.com/ninja-build/ninja.git", ref = opts.version },
    { source = "https://github.com/google/googletest.git", ref = "v1.16.0" }
  }
end

BUILD = function(install_dir, stage_dir, fetch_dir, tmp_dir, opts)
  local cmd = envy.template([[
{{python}} ./configure.py --bootstrap --gtest-source-dir={{googletest}}
./ninja all
./ninja_test
  ]], {
    python = envy.product("python3"),
    googletest = stage_dir .. "googletest.git"
  })

  envy.run(cmd, { cwd = stage_dir .. "ninja.git" })
end

INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir, opts)
  envy.move(stage_dir .. "ninja.git/ninja" .. envy.EXE_EXT, install_dir)
end

PRODUCTS = { ninja = "ninja" .. envy.EXE_EXT }

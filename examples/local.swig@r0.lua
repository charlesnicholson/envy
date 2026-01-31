IDENTITY = "local.swig@r0"

VALIDATE = function(opts)
  if opts.version == nil then
    return "'version' is a required option, e.g. '4.4.1'"
  end
end

FETCH = function(tmp_dir, opts)
  if envy.PLATFORM == "windows" then
    return "http://prdownloads.sourceforge.net/swig/swigwin-" .. opts.version .. ".zip"
  else
    return "http://prdownloads.sourceforge.net/swig/swig-" .. opts.version .. ".tar.gz"
  end
end

STAGE = { strip = 1 }

BUILD = function(install_dir, stage_dir, fetch_dir, tmp_dir, opts)
  if envy.PLATFORM == "windows" then
    return
  end

  envy.run(envy.template([[
  ./configure --prefix={{prefix}}
  make -j
]], { prefix = install_dir }))
end

INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir, opts)
  if envy.PLATFORM == "windows" then
    envy.move(stage_dir .. "swig.exe", install_dir .. "swig.exe")
  else
    envy.run("make install", { cwd = stage_dir })
  end
end

local bin = (envy.PLATFORM == "windows") and "" or "bin/"
PRODUCTS = { swig = bin .. "swig" .. envy.EXE_EXT }

IDENTITY = "local.ragel@r0"
FETCH = "https://www.colm.net/files/ragel/ragel-6.10.tar.gz"
STAGE = { strip = 1 }

BUILD = function(stage_dir, fetch_dir, tmp_dir, opts)
  local cmd = envy.template([[
  ./configure --prefix={{out}}
  make -j
  make install
]], {out = stage_dir .. "out" })

  envy.run(cmd)
end

INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir, opts)
  envy.move(stage_dir .. "out/bin/ragel", install_dir .. "ragel")
end

PRODUCTS = { ragel = "ragel" }

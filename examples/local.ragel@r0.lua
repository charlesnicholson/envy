-- @envy schema "1"
IDENTITY = "local.ragel@r0"
FETCH = "https://www.colm.net/files/ragel/ragel-6.10.tar.gz"
STAGE = { strip = 1 }

BUILD = function(install_dir, stage_dir, fetch_dir, tmp_dir, opts)
  envy.run(envy.template([[
  ./configure --prefix={{prefix}}
  make -j
]], { prefix = install_dir }))
end

INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir, opts)
  envy.run("make install", { cwd = stage_dir })
end

PRODUCTS = { ragel = "bin/ragel" }

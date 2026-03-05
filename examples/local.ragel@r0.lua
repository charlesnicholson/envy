-- @envy schema "1"
IDENTITY = "local.ragel@r0"
EXPORTABLE = true
FETCH = "https://www.colm.net/files/ragel/ragel-6.10.tar.gz"
STAGE = { strip = 1 }

BUILD = function(install_dir, stage_dir, fetch_dir, tmp_dir, opts)
  return envy.template([[
  ./configure --prefix={{prefix}}
  make -j
]], { prefix = install_dir })
end

INSTALL = "make install"

PRODUCTS = { ragel = "bin/ragel" }

-- @envy schema "1"
IDENTITY = "local.brew@r0"
PLATFORMS = { "darwin" }

CHECK = "brew --version"

INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir, opts)
  print("installing brew")
end

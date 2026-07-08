-- @envy schema "1"
IDENTITY = "local.brew@r0"
PLATFORMS = { "darwin" }
USER_MANAGED = true

SETUP = {
  brew = {
    CHECK = "brew --version",

    INSTALL = function(pkg_dir, opts)
      print("installing brew")
    end,
  },
}

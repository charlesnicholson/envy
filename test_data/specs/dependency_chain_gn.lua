IDENTITY = "local.gn@r0"
USER_MANAGED = true
SETUP = {
  main = {
    CHECK = function(pkg_dir, opts) return true end,
    INSTALL = function(pkg_dir, opts) end,
  },
}
DEPENDENCIES = {
  { spec = "local.ninja@r0", source = "simple_ninja.lua" }
}

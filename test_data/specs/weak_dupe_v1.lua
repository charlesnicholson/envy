-- First candidate for ambiguity tests
IDENTITY = "local.dupe@v1"
USER_MANAGED = true
DEPENDENCIES = {}

SETUP = {
  main = {
    CHECK = function(pkg_dir, options)
  return false
    end,
    INSTALL = function(pkg_dir, options)
  -- Programmatic install: no cache artifacts
    end,
  },
}

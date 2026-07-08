-- Fallback that should be ignored when a strong match exists
IDENTITY = "local.unused_fallback@v1"
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

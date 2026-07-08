-- Terminal dependency for the weak chain
IDENTITY = "local.chain_c@v1"
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

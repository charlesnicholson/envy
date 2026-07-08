-- Fallback that itself carries a weak reference
IDENTITY = "local.chain_b@v1"
USER_MANAGED = true
DEPENDENCIES = {
  { spec = "local.chain_c", weak = { spec = "local.chain_c@v1", source = "weak_chain_c.lua" } },
}

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

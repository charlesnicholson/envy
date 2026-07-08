-- Weak reference whose fallback introduces another weak reference (multi-iteration)
IDENTITY = "local.weak_chain_root@v1"
USER_MANAGED = true
DEPENDENCIES = {
  { spec = "local.chain_missing", weak = { spec = "local.chain_b@v1", source = "weak_chain_b.lua" } },
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

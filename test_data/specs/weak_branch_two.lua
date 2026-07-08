-- Second fallback that depends on the same shared target
IDENTITY = "local.branch_two@v1"
USER_MANAGED = true
DEPENDENCIES = {
  { spec = "local.shared", weak = { spec = "local.shared@v1", source = "weak_shared.lua" } },
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

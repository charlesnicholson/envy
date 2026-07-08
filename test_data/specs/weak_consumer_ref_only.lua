-- Reference-only dependency that is satisfied by an existing provider
IDENTITY = "local.weak_consumer_ref_only@v1"
USER_MANAGED = true
DEPENDENCIES = {
  { spec = "local.weak_provider@v1", source = "weak_provider.lua" },
  { spec = "local.weak_provider" },
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

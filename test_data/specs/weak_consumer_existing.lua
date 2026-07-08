-- Weak dependency with a present strong match and unused fallback
IDENTITY = "local.weak_consumer_existing@v1"
USER_MANAGED = true
DEPENDENCIES = {
  { spec = "local.existing_dep@v1", source = "weak_existing_dep.lua" },
  { spec = "local.existing_dep", weak = { spec = "local.unused_fallback@v1", source = "weak_unused_fallback.lua" } },
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

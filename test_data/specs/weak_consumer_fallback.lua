-- Weak dependency with fallback when the target is absent
IDENTITY = "local.weak_consumer_fallback@v1"
USER_MANAGED = true
DEPENDENCIES = {
  { spec = "local.missing_dep", weak = { spec = "local.weak_fallback@v1", source = "weak_fallback.lua" } },
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

-- Reference-only dependency that matches multiple strong candidates
IDENTITY = "local.weak_consumer_ambiguous@v1"
USER_MANAGED = true
DEPENDENCIES = {
  { spec = "local.dupe@v1", source = "weak_dupe_v1.lua" },
  { spec = "local.dupe@v2", source = "weak_dupe_v2.lua" },
  { spec = "local.dupe" },
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

-- Reference-only dependency with no provider anywhere in the graph
IDENTITY = "local.weak_missing_ref@v1"
USER_MANAGED = true
DEPENDENCIES = {
  { spec = "local.never_provided" },
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

-- Two weak fallbacks that each introduce new weak references; unresolved count stays flat
IDENTITY = "local.weak_progress_flat_root@v1"
USER_MANAGED = true
DEPENDENCIES = {
  { spec = "local.branch_one", weak = { spec = "local.branch_one@v1", source = "weak_branch_one.lua" } },
  { spec = "local.branch_two", weak = { spec = "local.branch_two@v1", source = "weak_branch_two.lua" } },
  { spec = "local.shared" },
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

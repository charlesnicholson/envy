-- Two weak fallbacks that each introduce new weak references; unresolved count stays flat
IDENTITY = "local.weak_progress_flat_root@v1"
DEPENDENCIES = {
  { spec = "local.branch_one", weak = { spec = "local.branch_one@v1", source = "weak_branch_one.lua" } },
  { spec = "local.branch_two", weak = { spec = "local.branch_two@v1", source = "weak_branch_two.lua" } },
  { spec = "local.shared" },
}

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- Programmatic install: no cache artifacts
end


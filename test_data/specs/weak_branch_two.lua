-- Second fallback that depends on the same shared target
IDENTITY = "local.branch_two@v1"
DEPENDENCIES = {
  { spec = "local.shared", weak = { spec = "local.shared@v1", source = "weak_shared.lua" } },
}

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- Programmatic install: no cache artifacts
end


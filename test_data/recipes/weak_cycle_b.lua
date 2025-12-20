-- Recipe B (matches "foo" query) that depends on A, creating cycle
IDENTITY = "local.foo@v1"
DEPENDENCIES = {
  { recipe = "local.weak_cycle_a@v1", source = "weak_cycle_a.lua" }
}

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- Programmatic install
end

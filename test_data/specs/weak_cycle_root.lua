-- Root recipe that loads both A and B before weak resolution
IDENTITY = "local.weak_cycle_root@v1"
DEPENDENCIES = {
  { spec = "local.weak_cycle_a@v1", source = "weak_cycle_a.lua" },
  { spec = "local.foo@v1", source = "weak_cycle_b.lua" },
}

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- Programmatic install
end

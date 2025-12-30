-- Recipe B in a cycle: B -> A
IDENTITY = "local.cycle_b@v1"
DEPENDENCIES = {
  { spec = "local.cycle_a@v1", source = "cycle_a.lua" }
}

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- Programmatic package
end

-- Recipe A in a cycle: A -> B
IDENTITY = "local.cycle_a@v1"
DEPENDENCIES = {
  { recipe = "local.cycle_b@v1", source = "cycle_b.lua" }
}

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- Programmatic package
end

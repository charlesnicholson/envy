-- Top of diamond: A depends on B and C (which both depend on D)
IDENTITY = "local.diamond_a@v1"
DEPENDENCIES = {
  { spec = "local.diamond_b@v1", source = "diamond_b.lua" },
  { spec = "local.diamond_c@v1", source = "diamond_c.lua" }
}

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- Programmatic package
end

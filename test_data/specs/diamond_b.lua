-- Left side of diamond: B depends on D
IDENTITY = "local.diamond_b@v1"
DEPENDENCIES = {
  { spec = "local.diamond_d@v1", source = "diamond_d.lua" }
}

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- Programmatic package
end

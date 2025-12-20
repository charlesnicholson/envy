-- Recipe with a dependency
IDENTITY = "local.withdep@v1"
DEPENDENCIES = {
  { recipe = "local.simple@v1", source = "simple.lua" }
}

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- Programmatic package - no cache interaction
end

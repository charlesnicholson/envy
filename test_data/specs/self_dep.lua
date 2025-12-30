-- Recipe that depends on itself (self-loop)
IDENTITY = "local.self_dep@v1"
DEPENDENCIES = {
  { spec = "local.self_dep@v1", source = "self_dep.lua" }
}

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- Programmatic package
end

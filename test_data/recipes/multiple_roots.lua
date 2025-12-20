-- Recipe with two independent dependencies
IDENTITY = "local.multiple_roots@v1"
DEPENDENCIES = {
  { recipe = "local.independent_left@v1", source = "independent_left.lua" },
  { recipe = "local.independent_right@v1", source = "independent_right.lua" }
}

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- Programmatic package
end

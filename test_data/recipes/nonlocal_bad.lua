-- remote.badrecipe@v1
-- Security test: non-local recipe trying to depend on local.* recipe

IDENTITY = "remote.badrecipe@v1"
DEPENDENCIES = {
  {
    recipe = "local.simple@v1",
    source = "simple.lua"
  }
}

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  envy.info("Installing bad recipe")
end

-- local.parent@v1
-- Local spec depending on another local recipe

IDENTITY = "local.parent@v1"
DEPENDENCIES = {
  {
    spec = "local.child@v1",
    source = "local_child.lua"
  }
}

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  envy.info("Installing local parent recipe")
end

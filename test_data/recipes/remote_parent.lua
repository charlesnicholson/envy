-- remote.parent@v1
-- Remote recipe depending on another remote recipe

IDENTITY = "remote.parent@v1"
DEPENDENCIES = {
  {
    recipe = "remote.child@v1",
    source = "remote_child.lua"
    -- No SHA256 (permissive mode - for testing)
  }
}

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  envy.info("Installing remote parent recipe")
end

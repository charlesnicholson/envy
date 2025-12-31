-- local.wrapper@v1
-- Local spec depending on remote recipe

IDENTITY = "local.wrapper@v1"
DEPENDENCIES = {
  {
    spec = "remote.base@v1",
    source = "remote_base.lua"
    -- No SHA256 (permissive mode - for testing)
  }
}

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  envy.info("Installing local wrapper recipe")
end

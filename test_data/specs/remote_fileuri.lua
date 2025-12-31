-- remote.fileuri@v1
-- Remote spec with no dependencies

IDENTITY = "remote.fileuri@v1"

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  envy.info("Installing remote fileuri recipe")
end

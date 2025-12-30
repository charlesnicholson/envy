-- remote.child@v1
-- Remote recipe with no dependencies

IDENTITY = "remote.child@v1"

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  envy.info("Installing remote child recipe")
end

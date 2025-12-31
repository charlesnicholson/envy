-- local.c@v1
-- Local spec with no dependencies

IDENTITY = "local.c@v1"

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  envy.info("Installing local c recipe")
end

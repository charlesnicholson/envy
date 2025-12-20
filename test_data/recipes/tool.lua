-- Minimal tool recipe used as dependency
IDENTITY = "local.tool@v1"
DEPENDENCIES = {}

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- Install tool
end

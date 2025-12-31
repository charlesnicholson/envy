-- Spec with wrong identity declaration (mismatch)
IDENTITY = "local.wrong_identity@v1"
DEPENDENCIES = {}

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- This should never execute
end

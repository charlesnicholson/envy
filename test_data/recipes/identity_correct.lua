-- Recipe with correct identity declaration (valid)
IDENTITY = "local.identity_correct@v1"
DEPENDENCIES = {}

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  envy.info("Identity validation passed")
end

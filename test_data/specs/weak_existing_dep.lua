-- Strong dependency that should satisfy weak queries
IDENTITY = "local.existing_dep@v1"
USER_MANAGED = true
DEPENDENCIES = {}

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- Programmatic install: no cache artifacts
end


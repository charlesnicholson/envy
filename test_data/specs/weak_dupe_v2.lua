-- Second candidate for ambiguity tests
IDENTITY = "local.dupe@v2"
USER_MANAGED = true
DEPENDENCIES = {}

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- Programmatic install: no cache artifacts
end


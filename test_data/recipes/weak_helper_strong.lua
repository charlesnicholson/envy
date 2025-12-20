-- Strong provider for helper identity
IDENTITY = "local.helper@v1"

function CHECK(project_root, options)
  return true
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- No-op; check returns true so install is skipped.
end

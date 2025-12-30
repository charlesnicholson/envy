-- Fallback helper for weak custom fetch dependency
IDENTITY = "local.helper.fallback@v1"

function CHECK(project_root, options)
  return true
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- No-op for helper fallback; check returns true so install is skipped.
end

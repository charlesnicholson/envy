IDENTITY = "local.ctx_package_user_provider@v1"

function CHECK(project_root, options)
  return true
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- User-managed: ephemeral workspace, no persistent cache artifacts
end

IDENTITY = "test.validate_error@v1"

VALIDATE = function(opts)
  error("boom")
end

CHECK = function(project_root, options) return true end
INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir, options) end

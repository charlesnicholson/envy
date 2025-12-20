IDENTITY = "test.validate_false@v1"

VALIDATE = function(opts)
  return false
end

CHECK = function(project_root, options) return true end
INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir, options) end

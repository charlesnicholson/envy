IDENTITY = "test.validate_ok@v1"

VALIDATE = function(opts)
  if opts and opts.foo then
    assert(opts.foo == "bar")
  end
end

CHECK = function(project_root, options) return true end
INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir, options) end

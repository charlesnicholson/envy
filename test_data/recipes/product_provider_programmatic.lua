-- Programmatic provider (user-managed) returning raw product value
IDENTITY = "local.product_programmatic@v1"
PRODUCTS = { tool = "programmatic-tool" }

CHECK = function(project_root, options)
  return true  -- Already satisfied; no cache artifact
end

INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- User-managed; no cache artifact
end

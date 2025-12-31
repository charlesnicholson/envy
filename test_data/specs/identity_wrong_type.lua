-- Spec with identity as wrong type (table instead of string)
IDENTITY = { name = "wrong" }
DEPENDENCIES = {}

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- This should never execute
end

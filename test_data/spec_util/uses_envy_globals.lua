-- Test spec that uses envy globals at parse time
IDENTITY = "test.uses_envy_globals@v1"
DEPENDENCIES = {}

-- This requires envy table to be available at global scope
PRODUCTS = { tool = "tool" .. envy.EXE_EXT }

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
end

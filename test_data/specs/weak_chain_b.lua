-- Fallback that itself carries a weak reference
IDENTITY = "local.chain_b@v1"
DEPENDENCIES = {
  { spec = "local.chain_c", weak = { spec = "local.chain_c@v1", source = "weak_chain_c.lua" } },
}

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- Programmatic install: no cache artifacts
end


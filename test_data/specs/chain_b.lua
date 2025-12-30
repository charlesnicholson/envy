-- Deep chain: B -> C
IDENTITY = "local.chain_b@v1"
DEPENDENCIES = {
  { spec = "local.chain_c@v1", source = "chain_c.lua" }
}

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- Programmatic package
end

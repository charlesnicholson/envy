-- Deep chain: C -> D
IDENTITY = "local.chain_c@v1"
DEPENDENCIES = {
  { recipe = "local.chain_d@v1", source = "chain_d.lua" }
}

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- Programmatic package
end

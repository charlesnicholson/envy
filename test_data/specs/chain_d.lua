-- Deep chain: D -> E
IDENTITY = "local.chain_d@v1"
DEPENDENCIES = {
  { spec = "local.chain_e@v1", source = "chain_e.lua" }
}

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- Programmatic package
end

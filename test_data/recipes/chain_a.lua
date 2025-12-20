-- Deep chain: A -> B
IDENTITY = "local.chain_a@v1"
DEPENDENCIES = {
  { recipe = "local.chain_b@v1", source = "chain_b.lua" }
}

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- Programmatic package
end

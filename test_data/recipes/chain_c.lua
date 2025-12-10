-- Deep chain: C -> D
IDENTITY = "local.chain_c@v1"
DEPENDENCIES = {
  { recipe = "local.chain_d@v1", source = "chain_d.lua" }
}

function CHECK(ctx)
  return false
end

function INSTALL(ctx)
  -- Programmatic package
end

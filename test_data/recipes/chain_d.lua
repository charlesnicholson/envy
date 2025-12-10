-- Deep chain: D -> E
IDENTITY = "local.chain_d@v1"
DEPENDENCIES = {
  { recipe = "local.chain_e@v1", source = "chain_e.lua" }
}

function CHECK(ctx)
  return false
end

function INSTALL(ctx)
  -- Programmatic package
end

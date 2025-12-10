-- Deep chain: B -> C
IDENTITY = "local.chain_b@v1"
DEPENDENCIES = {
  { recipe = "local.chain_c@v1", source = "chain_c.lua" }
}

function CHECK(ctx)
  return false
end

function INSTALL(ctx)
  -- Programmatic package
end

-- Deep chain: A -> B
IDENTITY = "local.chain_a@v1"
DEPENDENCIES = {
  { recipe = "local.chain_b@v1", source = "chain_b.lua" }
}

function CHECK(ctx)
  return false
end

function INSTALL(ctx)
  -- Programmatic package
end

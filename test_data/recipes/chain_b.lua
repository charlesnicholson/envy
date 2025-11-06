-- Deep chain: B -> C
identity = "local.chain_b@v1"
dependencies = {
  { recipe = "local.chain_c@v1", file = "chain_c.lua" }
}

function check(ctx)
  return false
end

function install(ctx)
  -- Programmatic package
end

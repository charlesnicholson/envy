-- Deep chain: C -> D
identity = "local.chain_c@v1"
dependencies = {
  { recipe = "local.chain_d@v1", file = "chain_d.lua" }
}

function check(ctx)
  return false
end

function install(ctx)
  -- Programmatic package
end

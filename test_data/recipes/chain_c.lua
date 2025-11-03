-- Deep chain: C -> D
dependencies = {
  { recipe = "local.chain_d@1.0.0", file = "chain_d.lua" }
}

function check(ctx)
  return false
end

function install(ctx)
  -- Programmatic package
end

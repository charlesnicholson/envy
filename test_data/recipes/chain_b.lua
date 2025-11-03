-- Deep chain: B -> C
dependencies = {
  { recipe = "local.chain_c@1.0.0", file = "chain_c.lua" }
}

function check(ctx)
  return false
end

function install(ctx)
  -- Programmatic package
end

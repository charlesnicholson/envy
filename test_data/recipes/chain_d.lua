-- Deep chain: D -> E
identity = "local.chain_d@v1"
dependencies = {
  { recipe = "local.chain_e@v1", file = "chain_e.lua" }
}

function check(ctx)
  return false
end

function install(ctx)
  -- Programmatic package
end

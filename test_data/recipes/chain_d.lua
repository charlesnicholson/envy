-- Deep chain: D -> E
dependencies = {
  { recipe = "local.chain_e@1.0.0", file = "chain_e.lua" }
}

function check(ctx)
  return false
end

function install(ctx)
  -- Programmatic package
end

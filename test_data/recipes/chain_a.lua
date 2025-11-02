-- Deep chain: A -> B
dependencies = {
  { recipe = "local.chain_b@1.0.0", file = "chain_b.lua" }
}

function check(ctx)
  return false
end

function install(ctx)
  -- Programmatic package
end

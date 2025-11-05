-- Deep chain: A -> B
identity = "local.chain_a@v1"
dependencies = {
  { recipe = "local.chain_b@v1", file = "chain_b.lua" }
}

function check(ctx)
  return false
end

function install(ctx)
  -- Programmatic package
end

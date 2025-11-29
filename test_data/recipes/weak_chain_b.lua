-- Fallback that itself carries a weak reference
identity = "local.chain_b@v1"
dependencies = {
  { recipe = "local.chain_c", weak = { recipe = "local.chain_c@v1", source = "weak_chain_c.lua" } },
}

function check(ctx)
  return false
end

function install(ctx)
  -- Programmatic install: no cache artifacts
end


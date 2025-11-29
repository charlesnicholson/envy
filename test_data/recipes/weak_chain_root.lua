-- Weak reference whose fallback introduces another weak reference (multi-iteration)
identity = "local.weak_chain_root@v1"
dependencies = {
  { recipe = "local.chain_missing", weak = { recipe = "local.chain_b@v1", source = "weak_chain_b.lua" } },
}

function check(ctx)
  return false
end

function install(ctx)
  -- Programmatic install: no cache artifacts
end


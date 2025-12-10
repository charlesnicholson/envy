-- Weak reference whose fallback introduces another weak reference (multi-iteration)
IDENTITY = "local.weak_chain_root@v1"
DEPENDENCIES = {
  { recipe = "local.chain_missing", weak = { recipe = "local.chain_b@v1", source = "weak_chain_b.lua" } },
}

function CHECK(ctx)
  return false
end

function INSTALL(ctx)
  -- Programmatic install: no cache artifacts
end


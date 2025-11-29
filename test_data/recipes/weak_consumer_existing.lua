-- Weak dependency with a present strong match and unused fallback
identity = "local.weak_consumer_existing@v1"
dependencies = {
  { recipe = "local.existing_dep@v1", source = "weak_existing_dep.lua" },
  { recipe = "local.existing_dep", weak = { recipe = "local.unused_fallback@v1", source = "weak_unused_fallback.lua" } },
}

function check(ctx)
  return false
end

function install(ctx)
  -- Programmatic install: no cache artifacts
end


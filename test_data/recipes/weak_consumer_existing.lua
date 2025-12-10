-- Weak dependency with a present strong match and unused fallback
IDENTITY = "local.weak_consumer_existing@v1"
DEPENDENCIES = {
  { recipe = "local.existing_dep@v1", source = "weak_existing_dep.lua" },
  { recipe = "local.existing_dep", weak = { recipe = "local.unused_fallback@v1", source = "weak_unused_fallback.lua" } },
}

function CHECK(ctx)
  return false
end

function INSTALL(ctx)
  -- Programmatic install: no cache artifacts
end


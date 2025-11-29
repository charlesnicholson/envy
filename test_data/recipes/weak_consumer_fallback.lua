-- Weak dependency with fallback when the target is absent
identity = "local.weak_consumer_fallback@v1"
dependencies = {
  { recipe = "local.missing_dep", weak = { recipe = "local.weak_fallback@v1", source = "weak_fallback.lua" } },
}

function check(ctx)
  return false
end

function install(ctx)
  -- Programmatic install: no cache artifacts
end


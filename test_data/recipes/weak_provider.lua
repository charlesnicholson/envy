-- Provides a concrete recipe for weak/reference consumers
identity = "local.weak_provider@v1"
dependencies = {}

function check(ctx)
  return false
end

function install(ctx)
  -- Programmatic install: no cache artifacts
end


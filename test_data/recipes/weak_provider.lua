-- Provides a concrete recipe for weak/reference consumers
IDENTITY = "local.weak_provider@v1"
DEPENDENCIES = {}

function CHECK(ctx)
  return false
end

function INSTALL(ctx)
  -- Programmatic install: no cache artifacts
end


-- Fallback recipe used when no provider is found
IDENTITY = "local.weak_fallback@v1"
DEPENDENCIES = {}

function CHECK(ctx)
  return false
end

function INSTALL(ctx)
  -- Programmatic install: no cache artifacts
end


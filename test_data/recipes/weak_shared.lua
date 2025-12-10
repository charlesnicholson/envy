-- Shared dependency produced by fallbacks
IDENTITY = "local.shared@v1"
DEPENDENCIES = {}

function CHECK(ctx)
  return false
end

function INSTALL(ctx)
  -- Programmatic install: no cache artifacts
end


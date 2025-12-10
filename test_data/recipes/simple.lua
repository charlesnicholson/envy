-- Minimal test recipe - no dependencies
IDENTITY = "local.simple@v1"
DEPENDENCIES = {}

function CHECK(ctx)
  return false
end

function INSTALL(ctx)
  -- Programmatic package - no cache interaction
end

-- Minimal tool recipe used as dependency
IDENTITY = "local.tool@v1"
DEPENDENCIES = {}

function CHECK(ctx)
  return false
end

function INSTALL(ctx)
  -- Install tool
end

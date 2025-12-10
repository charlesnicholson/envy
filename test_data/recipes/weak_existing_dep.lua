-- Strong dependency that should satisfy weak queries
IDENTITY = "local.existing_dep@v1"
DEPENDENCIES = {}

function CHECK(ctx)
  return false
end

function INSTALL(ctx)
  -- Programmatic install: no cache artifacts
end


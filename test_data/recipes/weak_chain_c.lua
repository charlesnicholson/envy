-- Terminal dependency for the weak chain
IDENTITY = "local.chain_c@v1"
DEPENDENCIES = {}

function CHECK(ctx)
  return false
end

function INSTALL(ctx)
  -- Programmatic install: no cache artifacts
end


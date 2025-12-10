-- Second candidate for ambiguity tests
IDENTITY = "local.dupe@v2"
DEPENDENCIES = {}

function CHECK(ctx)
  return false
end

function INSTALL(ctx)
  -- Programmatic install: no cache artifacts
end


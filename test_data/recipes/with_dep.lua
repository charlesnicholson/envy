-- Recipe with a dependency
IDENTITY = "local.withdep@v1"
DEPENDENCIES = {
  { recipe = "local.simple@v1", source = "simple.lua" }
}

function CHECK(ctx)
  return false
end

function INSTALL(ctx)
  -- Programmatic package - no cache interaction
end

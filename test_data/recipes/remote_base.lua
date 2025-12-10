-- remote.base@v1
-- Remote recipe with no dependencies

IDENTITY = "remote.base@v1"

function CHECK(ctx)
  return false
end

function INSTALL(ctx)
  envy.info("Installing remote base recipe")
end

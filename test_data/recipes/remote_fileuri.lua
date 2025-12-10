-- remote.fileuri@v1
-- Remote recipe with no dependencies

IDENTITY = "remote.fileuri@v1"

function CHECK(ctx)
  return false
end

function INSTALL(ctx)
  envy.info("Installing remote fileuri recipe")
end

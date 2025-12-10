-- remote.child@v1
-- Remote recipe with no dependencies

IDENTITY = "remote.child@v1"

function CHECK(ctx)
  return false
end

function INSTALL(ctx)
  envy.info("Installing remote child recipe")
end

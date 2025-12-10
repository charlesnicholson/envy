-- local.child@v1
-- Local recipe with no dependencies

IDENTITY = "local.child@v1"

function CHECK(ctx)
  return false
end

function INSTALL(ctx)
  envy.info("Installing local child recipe")
end

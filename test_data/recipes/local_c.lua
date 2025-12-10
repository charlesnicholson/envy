-- local.c@v1
-- Local recipe with no dependencies

IDENTITY = "local.c@v1"

function CHECK(ctx)
  return false
end

function INSTALL(ctx)
  envy.info("Installing local c recipe")
end

-- local.parent@v1
-- Local recipe depending on another local recipe

IDENTITY = "local.parent@v1"
DEPENDENCIES = {
  {
    recipe = "local.child@v1",
    source = "local_child.lua"
  }
}

function CHECK(ctx)
  return false
end

function INSTALL(ctx)
  envy.info("Installing local parent recipe")
end

-- remote.badrecipe@v1
-- Security test: non-local recipe trying to depend on local.* recipe

IDENTITY = "remote.badrecipe@v1"
DEPENDENCIES = {
  {
    recipe = "local.simple@v1",
    source = "simple.lua"
  }
}

function CHECK(ctx)
  return false
end

function INSTALL(ctx)
  envy.info("Installing bad recipe")
end

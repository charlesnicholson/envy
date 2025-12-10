-- remote.b@v1
-- Remote recipe that depends on local.* (transitively violates security)

IDENTITY = "remote.b@v1"
DEPENDENCIES = {
  {
    recipe = "local.c@v1",
    source = "local_c.lua"
  }
}

function CHECK(ctx)
  return false
end

function INSTALL(ctx)
  envy.info("Installing remote transitive b recipe")
end

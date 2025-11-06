-- remote.b@v1
-- Remote recipe that depends on local.* (transitively violates security)

identity = "remote.b@v1"
dependencies = {
  {
    recipe = "local.c@v1",
    file = "local_c.lua"
  }
}

function check(ctx)
  return false
end

function install(ctx)
  envy.info("Installing remote transitive b recipe")
end

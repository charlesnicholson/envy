-- remote.b@1.0.0
-- Remote recipe that depends on local.* (transitively violates security)

dependencies = {
  {
    recipe = "local.c@1.0.0",
    file = "local_c.lua"
  }
}

function check(ctx)
  return false
end

function install(ctx)
  envy.info("Installing remote transitive b recipe")
end

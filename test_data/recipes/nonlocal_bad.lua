-- remote.badrecipe@v1
-- Security test: non-local recipe trying to depend on local.* recipe

identity = "remote.badrecipe@v1"
dependencies = {
  {
    recipe = "local.simple@v1",
    file = "simple.lua"
  }
}

function check(ctx)
  return false
end

function install(ctx)
  envy.info("Installing bad recipe")
end

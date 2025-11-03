-- remote.badrecipe@1.0.0
-- Security test: non-local recipe trying to depend on local.* recipe

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

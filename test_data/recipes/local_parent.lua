-- local.parent@v1
-- Local recipe depending on another local recipe

identity = "local.parent@v1"
dependencies = {
  {
    recipe = "local.child@v1",
    source = "local_child.lua"
  }
}

function check(ctx)
  return false
end

function install(ctx)
  envy.info("Installing local parent recipe")
end

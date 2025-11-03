-- local.parent@1.0.0
-- Local recipe depending on another local recipe

dependencies = {
  {
    recipe = "local.child@1.0.0",
    file = "local_child.lua"
  }
}

function check(ctx)
  return false
end

function install(ctx)
  envy.info("Installing local parent recipe")
end

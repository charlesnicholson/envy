-- remote.parent@1.0.0
-- Remote recipe depending on another remote recipe

dependencies = {
  {
    recipe = "remote.child@1.0.0",
    url = "remote_child.lua",
    sha256 = "0000000000000000000000000000000000000000000000000000000000000000"
  }
}

function check(ctx)
  return false
end

function install(ctx)
  envy.info("Installing remote parent recipe")
end

-- remote.parent@v1
-- Remote recipe depending on another remote recipe

identity = "remote.parent@v1"
dependencies = {
  {
    recipe = "remote.child@v1",
    source = "remote_child.lua"
    -- No SHA256 (permissive mode - for testing)
  }
}

function check(ctx)
  return false
end

function install(ctx)
  envy.info("Installing remote parent recipe")
end

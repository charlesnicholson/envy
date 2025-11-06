-- local.wrapper@v1
-- Local recipe depending on remote recipe

identity = "local.wrapper@v1"
dependencies = {
  {
    recipe = "remote.base@v1",
    url = "remote_base.lua"
    -- No SHA256 (permissive mode - for testing)
  }
}

function check(ctx)
  return false
end

function install(ctx)
  envy.info("Installing local wrapper recipe")
end

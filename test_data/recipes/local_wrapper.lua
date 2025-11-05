-- local.wrapper@1.0.0
-- Local recipe depending on remote recipe

dependencies = {
  {
    recipe = "remote.base@1.0.0",
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

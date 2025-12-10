-- local.wrapper@v1
-- Local recipe depending on remote recipe

IDENTITY = "local.wrapper@v1"
DEPENDENCIES = {
  {
    recipe = "remote.base@v1",
    source = "remote_base.lua"
    -- No SHA256 (permissive mode - for testing)
  }
}

function CHECK(ctx)
  return false
end

function INSTALL(ctx)
  envy.info("Installing local wrapper recipe")
end

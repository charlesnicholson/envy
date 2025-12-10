-- remote.a@v1
-- Remote recipe that transitively depends on local.* through remote.b

IDENTITY = "remote.a@v1"
DEPENDENCIES = {
  {
    recipe = "remote.b@v1",
    source = "remote_transitive_b.lua"
    -- No SHA256 (permissive mode - for testing)
  }
}

function CHECK(ctx)
  return false
end

function INSTALL(ctx)
  envy.info("Installing remote transitive a recipe")
end

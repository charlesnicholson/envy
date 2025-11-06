-- remote.a@v1
-- Remote recipe that transitively depends on local.* through remote.b

identity = "remote.a@v1"
dependencies = {
  {
    recipe = "remote.b@v1",
    url = "remote_transitive_b.lua"
    -- No SHA256 (permissive mode - for testing)
  }
}

function check(ctx)
  return false
end

function install(ctx)
  envy.info("Installing remote transitive a recipe")
end

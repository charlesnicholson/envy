-- remote.a@1.0.0
-- Remote recipe that transitively depends on local.* through remote.b

dependencies = {
  {
    recipe = "remote.b@1.0.0",
    url = "remote_transitive_b.lua",
    sha256 = "0000000000000000000000000000000000000000000000000000000000000000"
  }
}

function check(ctx)
  return false
end

function install(ctx)
  envy.info("Installing remote transitive a recipe")
end

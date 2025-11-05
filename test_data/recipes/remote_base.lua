-- remote.base@v1
-- Remote recipe with no dependencies

identity = "remote.base@v1"

function check(ctx)
  return false
end

function install(ctx)
  envy.info("Installing remote base recipe")
end

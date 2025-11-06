-- remote.fileuri@v1
-- Remote recipe with no dependencies

identity = "remote.fileuri@v1"

function check(ctx)
  return false
end

function install(ctx)
  envy.info("Installing remote fileuri recipe")
end

-- remote.child@v1
-- Remote recipe with no dependencies

identity = "remote.child@v1"

function check(ctx)
  return false
end

function install(ctx)
  envy.info("Installing remote child recipe")
end

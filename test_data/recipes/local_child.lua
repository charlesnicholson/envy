-- local.child@v1
-- Local recipe with no dependencies

identity = "local.child@v1"

function check(ctx)
  return false
end

function install(ctx)
  envy.info("Installing local child recipe")
end

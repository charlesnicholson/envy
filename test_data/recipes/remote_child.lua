-- remote.child@1.0.0
-- Remote recipe with no dependencies

function check(ctx)
  return false
end

function install(ctx)
  envy.info("Installing remote child recipe")
end

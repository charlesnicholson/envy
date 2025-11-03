-- local.c@1.0.0
-- Local recipe with no dependencies

function check(ctx)
  return false
end

function install(ctx)
  envy.info("Installing local c recipe")
end

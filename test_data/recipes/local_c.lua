-- local.c@v1
-- Local recipe with no dependencies

identity = "local.c@v1"

function check(ctx)
  return false
end

function install(ctx)
  envy.info("Installing local c recipe")
end

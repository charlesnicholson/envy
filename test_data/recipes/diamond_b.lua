-- Left side of diamond: B depends on D
identity = "local.diamond_b@v1"
dependencies = {
  { recipe = "local.diamond_d@v1", file = "diamond_d.lua" }
}

function check(ctx)
  return false
end

function install(ctx)
  -- Programmatic package
end

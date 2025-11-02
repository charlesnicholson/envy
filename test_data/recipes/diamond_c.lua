-- Right side of diamond: C depends on D
dependencies = {
  { recipe = "local.diamond_d@1.0.0", file = "diamond_d.lua" }
}

function check(ctx)
  return false
end

function install(ctx)
  -- Programmatic package
end

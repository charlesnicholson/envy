-- Left side of diamond: B depends on D
IDENTITY = "local.diamond_b@v1"
DEPENDENCIES = {
  { recipe = "local.diamond_d@v1", source = "diamond_d.lua" }
}

function CHECK(ctx)
  return false
end

function INSTALL(ctx)
  -- Programmatic package
end

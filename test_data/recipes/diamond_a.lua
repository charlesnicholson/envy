-- Top of diamond: A depends on B and C (which both depend on D)
IDENTITY = "local.diamond_a@v1"
DEPENDENCIES = {
  { recipe = "local.diamond_b@v1", source = "diamond_b.lua" },
  { recipe = "local.diamond_c@v1", source = "diamond_c.lua" }
}

function CHECK(ctx)
  return false
end

function INSTALL(ctx)
  -- Programmatic package
end

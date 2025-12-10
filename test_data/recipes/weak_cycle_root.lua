-- Root recipe that loads both A and B before weak resolution
IDENTITY = "local.weak_cycle_root@v1"
DEPENDENCIES = {
  { recipe = "local.weak_cycle_a@v1", source = "weak_cycle_a.lua" },
  { recipe = "local.foo@v1", source = "weak_cycle_b.lua" },
}

function CHECK(ctx)
  return false
end

function INSTALL(ctx)
  -- Programmatic install
end

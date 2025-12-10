-- Recipe B in a cycle: B -> A
IDENTITY = "local.cycle_b@v1"
DEPENDENCIES = {
  { recipe = "local.cycle_a@v1", source = "cycle_a.lua" }
}

function CHECK(ctx)
  return false
end

function INSTALL(ctx)
  -- Programmatic package
end

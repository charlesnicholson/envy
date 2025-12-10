-- Recipe B (matches "foo" query) that depends on A, creating cycle
IDENTITY = "local.foo@v1"
DEPENDENCIES = {
  { recipe = "local.weak_cycle_a@v1", source = "weak_cycle_a.lua" }
}

function CHECK(ctx)
  return false
end

function INSTALL(ctx)
  -- Programmatic install
end

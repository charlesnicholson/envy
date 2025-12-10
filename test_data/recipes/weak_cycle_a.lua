-- Recipe A with weak reference that creates cycle after resolution
IDENTITY = "local.weak_cycle_a@v1"
DEPENDENCIES = {
  { recipe = "foo" },  -- Weak ref-only, will match weak_cycle_b
}

function CHECK(ctx)
  return false
end

function INSTALL(ctx)
  -- Programmatic install
end

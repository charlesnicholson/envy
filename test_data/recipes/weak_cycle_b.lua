-- Recipe B (matches "foo" query) that depends on A, creating cycle
identity = "local.foo@v1"
dependencies = {
  { recipe = "local.weak_cycle_a@v1", source = "weak_cycle_a.lua" }
}

function check(ctx)
  return false
end

function install(ctx)
  -- Programmatic install
end
